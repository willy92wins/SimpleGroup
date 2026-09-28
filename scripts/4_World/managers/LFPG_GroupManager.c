// ============================================================================
// LFPG_GroupManager.c - 4_World/managers
// Singleton SERVER-SIDE - el corazon del sistema de grupos
//
// OPTIMIZACIONES:
//  - m_PlayerToGroup: O(1) lookup para IsInBuildZone (hot path)
//  - m_GroupNames: O(1) validacion de nombres unicos
//  - m_FlagPositions: array para territory overlap (no GetObjectsAtPosition)
//  - Sin m_FlagToGroup (redundante: flag.m_GroupID es suficiente)
//  - Timer class para tick periodico (inmune al bug CallLater 4.5h)
//  - JSON save atomico (tmp -> bak -> final)
//  - RPC rate limiting por jugador
//  - Deploy/Garden counters O(1) (no proximity scan en runtime)
// ============================================================================

// Version del formato de groups.json. Un fichero con version mayor no se carga.
const int LFPG_GROUPS_FILE_VERSION = 1;

class LFPG_GroupManager
{
    // ========================================================================
    // SINGLETON
    // ========================================================================
    private static ref LFPG_GroupManager s_Instance;

    static LFPG_GroupManager Get()
    {
        return s_Instance;
    }

    static void Create()
    {
        if (!s_Instance)
        {
            s_Instance = new LFPG_GroupManager();
        }
    }

    static void Destroy()
    {
        s_Instance = null;
    }

    // ========================================================================
    // DATA STRUCTURES
    // ========================================================================
    protected ref LFPG_TerritoryConfig m_Config;

    // Primary storage: groupID -> GroupData
    protected ref map<string, ref LFPG_GroupData> m_Groups;

    // Fast lookups
    protected ref map<string, string> m_PlayerToGroup;
    protected ref map<string, bool> m_GroupNames;

    // Flag position cache - for territory overlap checks
    protected ref array<ref LFPG_FlagPositionCache> m_FlagPositions;

    // Flag entity references (groupID -> flag) - runtime only
    protected ref map<string, LFPG_FlagBase> m_GroupFlags;

    // RPC rate limiting: playerUID -> last RPC time (ms)
    protected ref map<string, int> m_RPCThrottle;

    // Periodic validation timer (Timer class, NOT CallLater)
    protected ref Timer m_ValidationTimer;

    // FIX H4+H5: Buffers reutilizables (no allocar en ticks)
    protected ref array<string> m_OrphanBuffer;
    protected ref array<Man> m_PlayerSearchBuffer;
    protected ref array<Object> m_RecalObjectBuffer;
    protected ref array<CargoBase> m_RecalCargoBuffer;

    // Banderas que cargaron ANTES que los grupos: el engine restaura las entidades
    // dentro de super.OnInit(), y LoadGroups() corre despues. Una bandera que no
    // encuentra su grupo NO es huerfana: es que aun no se ha cargado el JSON.
    protected ref array<LFPG_FlagBase> m_PendingFlags;

    // Ticks consecutivos que un grupo lleva sin bandera viva: groupID -> strikes.
    protected ref map<string, int> m_OrphanStrikes;

    // true mientras el server se apaga: no se disuelve nada durante el apagado.
    protected bool m_IsShuttingDown;

    // Red de seguridad: si al arrancar demasiados grupos aparecen sin bandera, el
    // estado se cargo mal. Se desactiva la disolucion automatica en esa sesion.
    protected bool m_DissolveDisabled;
    protected bool m_DissolveDisabledLogged;

    // FIX M2: Dirty flag para saves diferidos (counters)
    protected bool m_IsDirty;

    // FIX F: Recalibracion periodica de counters
    // Primer tick: siempre recalibrar (post-startup)
    // Ticks siguientes: cada m_RecalibrationIntervalSeconds (configurable, default 30s)
    protected bool m_NeedsCounterRecalibration;
    protected int m_RecalibrationTickCounter;

    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================
    void LFPG_GroupManager()
    {
        m_Groups = new map<string, ref LFPG_GroupData>;
        m_PlayerToGroup = new map<string, string>;
        m_GroupNames = new map<string, bool>;
        m_FlagPositions = new array<ref LFPG_FlagPositionCache>;
        m_GroupFlags = new map<string, LFPG_FlagBase>;
        m_RPCThrottle = new map<string, int>;
        m_OrphanBuffer = new array<string>;
        m_PlayerSearchBuffer = new array<Man>;
        m_RecalObjectBuffer = new array<Object>;
        m_RecalCargoBuffer = new array<CargoBase>;
        m_PendingFlags = new array<LFPG_FlagBase>;
        m_OrphanStrikes = new map<string, int>;
        m_IsShuttingDown = false;
        m_DissolveDisabled = false;
        m_DissolveDisabledLogged = false;
        m_IsDirty = false;
        m_NeedsCounterRecalibration = true;
        m_RecalibrationTickCounter = 0;
    }

    void ~LFPG_GroupManager()
    {
        if (m_ValidationTimer)
        {
            m_ValidationTimer.Stop();
            m_ValidationTimer = null;
        }
    }

    // ========================================================================
    // INIT - Llamado desde modded MissionServer.OnInit()
    // ========================================================================
    void Init()
    {
        // Un tmp residual NO se tira a ciegas: si el proceso murio despues de que
        // el final desapareciera, ese tmp es la unica copia buena que queda.
        string staleTmp = LFPG_TerritoryConfig.GetGroupsTmpPath();
        if (FileExist(staleTmp))
        {
            string tmpFinal = LFPG_TerritoryConfig.GetGroupsPath();
            bool tmpRecovered = false;

            if (!FileExist(tmpFinal))
            {
                LFPG_GroupsFileData tmpData = new LFPG_GroupsFileData();
                string tmpErr = "";
                if (JsonFileLoader<LFPG_GroupsFileData>.LoadFile(staleTmp, tmpData, tmpErr))
                {
                    if (tmpData.m_Groups && tmpData.m_Groups.Count() > 0)
                    {
                        if (CopyFile(staleTmp, tmpFinal))
                        {
                            tmpRecovered = true;
                            string recMsg = "Init: groups.json missing; recovered from tmp with ";
                            recMsg = recMsg + tmpData.m_Groups.Count().ToString();
                            recMsg = recMsg + " groups.";
                            LFPG_Log.Info(recMsg);
                        }
                    }
                }
            }

            if (!tmpRecovered)
            {
                DeleteFile(staleTmp);
                LFPG_Log.Info("Init: removed unusable tmp file.");
            }
        }

        // Cargar config
        m_Config = LFPG_TerritoryConfig.Load();

        // Cargar grupos desde JSON
        LoadGroups();

        // Los grupos acaban de entrar en memoria: las banderas que se restauraron
        // antes (durante super.OnInit) por fin pueden registrarse.
        ResolvePendingFlags();

        // Re-vinculacion por posicion + red de seguridad anti-wipe.
        AuditOrphansAtBoot();

        // Lo que siga pendiente es huerfano de verdad: todos los grupos estan ya en
        // memoria y no va a llegar ninguno mas. Vaciar el id AQUI si es correcto (a
        // diferencia del bug original, que decidia antes de cargar el JSON): la
        // bandera queda abandonada, sigue en el mundo y sigue negando su radio.
        int pendLeftCount = m_PendingFlags.Count();
        int pi;
        for (pi = 0; pi < pendLeftCount; pi = pi + 1)
        {
            LFPG_FlagBase pFlag = m_PendingFlags[pi];
            if (pFlag)
            {
                pFlag.SetGroupID("");
                RegisterAbandonedFlag(pFlag);
            }
        }
        m_PendingFlags.Clear();

        string pendLeft = "Init: flags declared abandoned after boot resolve: ";
        pendLeft = pendLeft + pendLeftCount.ToString();
        LFPG_Log.Info(pendLeft);

        // Timer periodico de validacion (FIX M-2: interval configurable)
        // Usa Timer class (NO CallLater) - inmune al bug de 4.5h
        float tickSec = 60.0;
        if (m_Config && m_Config.m_ValidationTickSeconds > 0)
            tickSec = m_Config.m_ValidationTickSeconds;
        m_ValidationTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        m_ValidationTimer.Run(tickSec, this, "OnValidationTick", null, true);

        string msg = "GroupManager initialized. Groups: ";
        msg = msg + m_Groups.Count().ToString();
        LFPG_Log.Info(msg);
    }

    LFPG_TerritoryConfig GetConfig()
    {
        return m_Config;
    }

    // ========================================================================
    // PENDING FLAGS - banderas que cargaron antes que los grupos
    // ========================================================================
    void RegisterPendingFlag(LFPG_FlagBase flag)
    {
        if (!flag)
            return;

        int count = m_PendingFlags.Count();
        int i;
        for (i = 0; i < count; i = i + 1)
        {
            if (m_PendingFlags[i] == flag)
                return;
        }

        m_PendingFlags.Insert(flag);
        string pendMsg = "Flag pending, group not loaded yet: ";
        pendMsg = pendMsg + flag.GetGroupID();
        LFPG_Log.Info(pendMsg);
    }

    // Idempotente: se puede llamar tantas veces como haga falta.
    // Recorre hacia atras porque borra elementos del array.
    void ResolvePendingFlags()
    {
        int count = m_PendingFlags.Count();
        int i;
        for (i = count - 1; i >= 0; i = i - 1)
        {
            LFPG_FlagBase flag = m_PendingFlags[i];
            if (!flag)
            {
                m_PendingFlags.Remove(i);
                continue;
            }

            if (GroupExists(flag.GetGroupID()))
            {
                flag.RetryRegisterWithManager();
                m_PendingFlags.Remove(i);
            }
            // Si el grupo sigue sin existir se queda en la lista: puede llegar
            // mas tarde. No se declara abandonada aqui y no se toca su groupID.
        }
    }

    // Auditoria de arranque. Se llama UNA vez en Init, tras resolver pendientes.
    //
    // Dos cometidos:
    //  1. Re-vincular banderas que perdieron su groupID en un arranque anterior.
    //     m_FlagPosition es la unica ancla que sobrevive a eso, y la bandera esta
    //     en el mundo (no en m_PendingFlags: entro por la rama de abandonada).
    //  2. Si aun asi demasiados grupos quedan sin bandera, el estado se cargo mal:
    //     se desactiva la disolucion automatica antes de que borre nada.
    void AuditOrphansAtBoot()
    {
        int total = m_Groups.Count();
        if (total == 0)
            return;

        int i;
        int j;

        for (i = 0; i < total; i = i + 1)
        {
            string bindID = m_Groups.GetKey(i);
            LFPG_FlagBase alreadyBound;
            if (m_GroupFlags.Find(bindID, alreadyBound))
                continue;

            LFPG_GroupData group = m_Groups.Get(bindID);
            if (!group)
                continue;

            vector anchorPos = group.m_FlagPosition;
            bool anchorUnset = anchorPos[0] == 0.0 && anchorPos[2] == 0.0;
            if (anchorUnset)
                continue;

            m_RecalObjectBuffer.Clear();
            m_RecalCargoBuffer.Clear();
            GetGame().GetObjectsAtPosition(anchorPos, 5.0, m_RecalObjectBuffer, m_RecalCargoBuffer);

            int objCount = m_RecalObjectBuffer.Count();
            for (j = 0; j < objCount; j = j + 1)
            {
                LFPG_FlagBase candidate = LFPG_FlagBase.Cast(m_RecalObjectBuffer[j]);
                if (!candidate)
                    continue;
                if (candidate.GetGroupID() != "")
                    continue;

                candidate.SetGroupID(bindID);
                if (RegisterFlag(candidate, bindID))
                {
                    RemoveAbandonedFlagPosition(candidate.GetPosition());
                    string rebindMsg = "Boot audit: re-bound orphan flag to group ";
                    rebindMsg = rebindMsg + bindID;
                    LFPG_Log.Info(rebindMsg);
                }
                break;
            }
        }

        int orphans = 0;
        for (i = 0; i < total; i = i + 1)
        {
            string checkID = m_Groups.GetKey(i);
            LFPG_FlagBase chkFlag;
            if (!m_GroupFlags.Find(checkID, chkFlag))
                orphans = orphans + 1;
        }

        // Arm on proportional loss, or whenever EVERY group lost its flag.
        // The >= 2 floor alone leaves a single-group server with no net,
        // which is the exact shape of a full wipe on a small server.
        bool massiveLoss = orphans >= 2 && orphans * 10 >= total * 3;
        if (orphans > 0 && orphans == total)
            massiveLoss = true;
        if (massiveLoss)
        {
            m_DissolveDisabled = true;
            string netMsg = "BOOT SAFETY NET: ";
            netMsg = netMsg + orphans.ToString();
            netMsg = netMsg + " of ";
            netMsg = netMsg + total.ToString();
            netMsg = netMsg + " groups have no flag. Automatic dissolve DISABLED this session.";
            LFPG_Log.Error(netMsg);
        }
    }

    bool IsShuttingDown()
    {
        return m_IsShuttingDown;
    }

    void SetShuttingDown()
    {
        m_IsShuttingDown = true;
    }

    // ========================================================================
    // PERIODIC VALIDATION (cada 60s)
    // NO recalcula raiseProgress (eso es lazy).
    // Solo valida integridad: grupos sin bandera -> disolver.
    // ========================================================================
    void OnValidationTick()
    {
        // Durante el apagado no se disuelve ni se persiste nada: un wipe de
        // entidades al cerrar disparia N EEDelete y vaciaria groups.json.
        if (m_IsShuttingDown)
            return;

        // Por si alguna bandera se restauro tarde.
        ResolvePendingFlags();

        // FIX F: Recalibracion periodica de counters
        // Primer tick: siempre recalibrar (post-startup, todas las entidades ya cargaron)
        // Ticks siguientes: cada m_Config.m_RecalibrationIntervalSeconds
        // Usa GetTime (ms) para no depender del intervalo del tick caller
        if (m_NeedsCounterRecalibration)
        {
            RecalibrateCounters();
            m_NeedsCounterRecalibration = false;
            m_RecalibrationTickCounter = GetGame().GetTime();
        }
        else
        {
            int now = GetGame().GetTime();
            int elapsed = now - m_RecalibrationTickCounter;
            int intervalMs = m_Config.m_RecalibrationIntervalSeconds * 1000;
            if (elapsed >= intervalMs)
            {
                RecalibrateCounters();
                m_RecalibrationTickCounter = now;
            }
        }

        // FIX H4: Reutilizar buffer en vez de new array
        m_OrphanBuffer.Clear();
        int i;
        int groupCount = m_Groups.Count();

        // Iterar grupos buscando huerfanos (grupo sin bandera valida)
        for (i = 0; i < groupCount; i = i + 1)
        {
            string groupID = m_Groups.GetKey(i);
            bool isOrphan = false;

            if (!m_GroupFlags.Contains(groupID))
            {
                isOrphan = true;
            }
            else
            {
                LFPG_FlagBase flag = m_GroupFlags.Get(groupID);
                if (!flag)
                {
                    isOrphan = true;
                    // Limpiar la entrada nula del map
                    m_GroupFlags.Remove(groupID);
                }
            }

            if (isOrphan)
            {
                // map.Get() no garantiza 0 para una clave ausente (enscript.c:847
                // documenta NULL). Find (enscript.c:858) devuelve bool y es la via segura.
                int strikes = 0;
                if (m_OrphanStrikes.Find(groupID, strikes))
                {
                    strikes = strikes + 1;
                }
                else
                {
                    strikes = 1;
                }
                m_OrphanStrikes.Set(groupID, strikes);

                if (strikes >= 3)
                {
                    m_OrphanBuffer.Insert(groupID);
                }
            }
            else
            {
                // Aparecio su bandera: el contador se reinicia.
                if (m_OrphanStrikes.Contains(groupID))
                {
                    m_OrphanStrikes.Remove(groupID);
                }
            }
        }

        // Si queda alguna bandera pendiente, el mundo no ha terminado de cargar y
        // cualquier "huerfano" es un falso positivo: no se disuelve nada este tick.
        if (m_DissolveDisabled)
        {
            if (!m_DissolveDisabledLogged)
            {
                m_DissolveDisabledLogged = true;
                LFPG_Log.Error("Orphan dissolve skipped: boot safety net is active this session.");
            }
        }
        else
        {
            int orphanCount = m_OrphanBuffer.Count();
            for (i = 0; i < orphanCount; i = i + 1)
            {
                string orphanID = m_OrphanBuffer[i];
                string warnMsg = "Orphan group dissolved after 3 strikes: ";
                warnMsg = warnMsg + orphanID;
                LFPG_Log.Error(warnMsg);
                DissolveGroup(orphanID);
                m_OrphanStrikes.Remove(orphanID);
            }
        }

        // Limpiar entradas huerfanas en m_FlagPositions
        // (posiciones de territorios cuyo grupo ya no existe)
        // FIX BUG2: Preservar entradas ABANDONED (banderas abandonadas que siguen en el mundo)
        int posCount = m_FlagPositions.Count();
        int j = 0;
        while (j < posCount)
        {
            LFPG_FlagPositionCache posCache = m_FlagPositions[j];
            if (!posCache || (!m_Groups.Contains(posCache.m_GroupID) && posCache.m_GroupID != LFPG_ABANDONED_GROUP))
            {
                string posWarn = "Stale position cache entry removed for group: ";
                if (posCache)
                {
                    posWarn = posWarn + posCache.m_GroupID;
                }
                else
                {
                    posWarn = posWarn + "(null)";
                }
                LFPG_Log.Debug(posWarn);
                m_FlagPositions.Remove(j);
                posCount = posCount - 1;
            }
            else
            {
                j = j + 1;
            }
        }

        // FIX M2: Flush saves diferidos de counters
        if (m_IsDirty)
        {
            SaveGroups();
            m_IsDirty = false;
        }

        // VISUAL SYNC: Actualizar SyncVar de progress en todas las banderas
        // para que los clientes vean el decay gradual (cada 60s)
        int flagCount = m_GroupFlags.Count();
        int fi = 0;
        for (fi = 0; fi < flagCount; fi = fi + 1)
        {
            LFPG_FlagBase syncFlag = m_GroupFlags.GetElement(fi);
            if (syncFlag)
            {
                syncFlag.RefreshVisualProgress();
            }
        }
    }

    // ========================================================================
    // FLAG REGISTRATION - Llamado desde flag.AfterStoreLoad() y al crear grupo
    // ========================================================================
    bool RegisterFlag(LFPG_FlagBase flag, string groupID)
    {
        if (!flag || groupID == "")
            return false;

        // FIX I-21: Si ya hay una flag registrada para este grupo y sigue viva,
        // rechazar el segundo register para evitar escenario con dos flags del mismo grupo
        // (admin-spawn bug). La flag "canonica" es la primera registrada.
        if (m_GroupFlags.Contains(groupID))
        {
            LFPG_FlagBase existing = m_GroupFlags.Get(groupID);
            if (existing && existing != flag)
            {
                string dupMsg = "Duplicate flag register for group ";
                dupMsg = dupMsg + groupID;
                dupMsg = dupMsg + ", rejecting second flag at ";
                dupMsg = dupMsg + flag.GetPosition().ToString();
                LFPG_Log.Error(dupMsg);
                return false;
            }
        }

        m_GroupFlags.Set(groupID, flag);
        UpdateFlagPositionCache(groupID, flag.GetPosition(), flag.ComputeCurrentRaiseProgress(), flag.GetTier());

        // FIX C4: Restaurar MemberCount desde datos del grupo (tras restart)
        // + Sincronizar posicion persistida para fallback en LIGHTWEIGHT_SYNC
        if (m_Groups.Contains(groupID))
        {
            LFPG_GroupData group = m_Groups.Get(groupID);
            if (group)
            {
                flag.SetMemberCount(group.GetMemberCount());
                group.m_FlagPosition = flag.GetPosition();
            }
        }

        return true;
    }

    void UnregisterFlag(string groupID)
    {
        if (m_GroupFlags.Contains(groupID))
        {
            m_GroupFlags.Remove(groupID);
        }
        RemoveFlagPositionCache(groupID);
    }

    // ========================================================================
    // FLAG POSITION CACHE - Para territory overlap checks
    // Array lineal: con 50 flags, 50 comparaciones es despreciable
    // Mucho mas barato que GetObjectsAtPosition(500m)
    // ========================================================================
    protected void UpdateFlagPositionCache(string groupID, vector pos, float progress, int tier)
    {
        int i;
        int count = m_FlagPositions.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_FlagPositionCache cache = m_FlagPositions[i];
            if (!cache)
                continue;

            // Match por groupID (comportamiento normal para updates)
            if (cache.m_GroupID == groupID)
            {
                cache.Set(pos, groupID, progress, tier);
                return;
            }

            // FIX BUG2: Match por posicion para entradas ABANDONED (al reclamar)
            // Cuando un jugador reclama una bandera abandonada, sobreescribir la entry
            if (cache.m_GroupID == LFPG_ABANDONED_GROUP)
            {
                float dx = pos[0] - cache.m_Position[0];
                float dz = pos[2] - cache.m_Position[2];
                float distSq = (dx * dx) + (dz * dz);
                if (distSq < 4.0)
                {
                    cache.Set(pos, groupID, progress, tier);
                    return;
                }
            }
        }

        // No existe - anadir nuevo
        LFPG_FlagPositionCache newCache = new LFPG_FlagPositionCache();
        newCache.Set(pos, groupID, progress, tier);
        m_FlagPositions.Insert(newCache);
    }

    protected void RemoveFlagPositionCache(string groupID)
    {
        int i;
        int count = m_FlagPositions.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_FlagPositionCache cache = m_FlagPositions[i];
            if (cache && cache.m_GroupID == groupID)
            {
                m_FlagPositions.Remove(i);
                return;
            }
        }
    }

    // ========================================================================
    // FIX BUG2: Limpiar posicion de bandera abandonada cuando despawna
    // Llamado desde LFPG_FlagBase.EEDelete cuando m_GroupID == ""
    // ========================================================================
    void RemoveAbandonedFlagPosition(vector pos)
    {
        int i;
        int count = m_FlagPositions.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_FlagPositionCache cache = m_FlagPositions[i];
            if (cache && cache.m_GroupID == LFPG_ABANDONED_GROUP)
            {
                float dx = pos[0] - cache.m_Position[0];
                float dz = pos[2] - cache.m_Position[2];
                float distSq = (dx * dx) + (dz * dz);
                if (distSq < 4.0)
                {
                    m_FlagPositions.Remove(i);
                    string logMsg = "Abandoned flag position cleaned at ";
                    logMsg = logMsg + pos.ToString();
                    LFPG_Log.Debug(logMsg);
                    return;
                }
            }
        }
    }

    // ========================================================================
    // FIX BUG2: Registrar bandera abandonada en cache (tras restart)
    // Llamado desde LFPG_FlagBase.AfterStoreLoad cuando m_GroupID == ""
    // ========================================================================
    void RegisterAbandonedFlag(LFPG_FlagBase flag)
    {
        if (!flag)
            return;

        UpdateFlagPositionCache(LFPG_ABANDONED_GROUP, flag.GetPosition(), 0.0, flag.GetTier());

        string logMsg = "Abandoned flag re-registered after restart at ";
        logMsg = logMsg + flag.GetPosition().ToString();
        LFPG_Log.Debug(logMsg);
    }

    // ========================================================================
    // HOT PATH: IsInBuildZone - O(1) - Llamado cada frame por hologram
    // ========================================================================
    bool IsInBuildZone(string playerUID, vector buildPos)
    {
        // 1. Lookup grupo del jugador - O(1)
        if (!m_PlayerToGroup.Contains(playerUID))
            return false;

        string groupID = m_PlayerToGroup.Get(playerUID);
        if (groupID == "")
            return false;

        // 2. Obtener datos del grupo - O(1)
        if (!m_Groups.Contains(groupID))
            return false;

        // 3. Obtener posicion de la bandera desde cache - O(f) peor caso
        //    pero tipicamente el jugador tiene UNA bandera, asi que es O(1) amortizado
        LFPG_FlagBase flag = null;
        if (m_GroupFlags.Contains(groupID))
        {
            flag = m_GroupFlags.Get(groupID);
        }

        if (!flag)
            return false;

        // 4. Check distancia 2D sin sqrt - O(1)
        // Usa solo XZ (ignora Y) para consistencia con cliente
        vector flagPos = flag.GetPosition();
        float dx = buildPos[0] - flagPos[0];
        float dz = buildPos[2] - flagPos[2];
        float distSq = (dx * dx) + (dz * dz);
        if (distSq > m_Config.m_BuildRadiusSq)
            return false;

        // 5. Check bandera levantada (lazy compute)
        float progress = flag.ComputeCurrentRaiseProgress();
        if (progress <= 0.0)
            return false;

        return true;
    }

    // ========================================================================
    // TERRITORY CHECK - ?Hay otra bandera a <500m de esta posicion?
    // Usa cache, NO GetObjectsAtPosition
    // ========================================================================
    bool IsPositionInTerritory(vector pos)
    {
        int i;
        int count = m_FlagPositions.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_FlagPositionCache cache = m_FlagPositions[i];
            if (cache)
            {
                float tdx = pos[0] - cache.m_Position[0];
                float tdz = pos[2] - cache.m_Position[2];
                float distSq = (tdx * tdx) + (tdz * tdz);
                if (distSq < m_Config.m_TerritoryRadiusSq)
                    return true;
            }
        }
        return false;
    }

    // ========================================================================
    // FIND GROUP BY POSITION - Helper para ModdedBBB y ModdedGardenPlot
    // FIX I-24: Retorna el groupID cuya distancia sea MENOR (no el primero)
    //           para consistencia cuando hay builds solapadas.
    // ========================================================================
    string FindGroupIDAtPosition(vector pos)
    {
        if (!m_Config)
            return "";

        string bestGroupID = "";
        float bestDistSq = m_Config.m_BuildRadiusSq;

        int j;
        int cnt = m_FlagPositions.Count();
        for (j = 0; j < cnt; j = j + 1)
        {
            LFPG_FlagPositionCache cacheEntry = m_FlagPositions[j];
            if (!cacheEntry)
                continue;
            // Excluir entradas ABANDONED - no son grupos activos
            if (cacheEntry.m_GroupID == LFPG_ABANDONED_GROUP)
                continue;

            float fdx = pos[0] - cacheEntry.m_Position[0];
            float fdz = pos[2] - cacheEntry.m_Position[2];
            float dSq = (fdx * fdx) + (fdz * fdz);
            if (dSq < bestDistSq)
            {
                bestDistSq = dSq;
                bestGroupID = cacheEntry.m_GroupID;
            }
        }
        return bestGroupID;
    }

    // Nearest other group whose flag lies inside the build radius (XZ) and whose
    // live raise progress is above zero. Cached progress is used only when that
    // group has no registered flag entity. Empty string means no foreign owner.
    // A groupless actor passes "" and any such group is foreign.
    // extraMeters widens the search past the build radius. Crafting uses it so a
    // result spawned ahead of the player cannot land inside a foreign zone.
    string GetForeignOwnerAt(vector pos, string actorGroupID, float extraMeters = 0)
    {
        if (!m_Config)
            return "";

        float searchRadius = m_Config.m_BuildRadiusMeters;
        if (extraMeters > 0.0)
            searchRadius = searchRadius + extraMeters;

        string nearestID = "";
        float nearestDistSq = searchRadius * searchRadius;

        int fo;
        int foCount = m_FlagPositions.Count();
        for (fo = 0; fo < foCount; fo = fo + 1)
        {
            LFPG_FlagPositionCache foEntry = m_FlagPositions[fo];
            if (!foEntry)
                continue;
            if (foEntry.m_GroupID == LFPG_ABANDONED_GROUP)
                continue;
            if (actorGroupID != "" && foEntry.m_GroupID == actorGroupID)
                continue;

            float fodx = pos[0] - foEntry.m_Position[0];
            float fodz = pos[2] - foEntry.m_Position[2];
            float foDistSq = (fodx * fodx) + (fodz * fodz);
            if (foDistSq >= nearestDistSq)
                continue;

            float liveProgress = foEntry.m_RaiseProgress;
            if (m_GroupFlags && m_GroupFlags.Contains(foEntry.m_GroupID))
            {
                LFPG_FlagBase foFlag = m_GroupFlags.Get(foEntry.m_GroupID);
                if (foFlag)
                    liveProgress = foFlag.ComputeCurrentRaiseProgress();
            }
            if (liveProgress <= 0.0)
                continue;

            nearestDistSq = foDistSq;
            nearestID = foEntry.m_GroupID;
        }
        return nearestID;
    }

    // ========================================================================
    // FIX G-2: True when a different group owns pos inside the build radius.
    // Uses the build radius (m_BuildRadiusSq), not the territory radius, and
    // the live raise progress from GetForeignOwnerAt.
    // Excludes LFPG_ABANDONED_GROUP and the actor's own group.
    // ========================================================================
    bool IsPositionInOtherTerritory(vector pos, string ownGroupID)
    {
        string foreignID = GetForeignOwnerAt(pos, ownGroupID);
        if (foreignID == "")
            return false;
        return true;
    }

    // ========================================================================
    // DEPLOY / GARDEN COUNTERS - O(1) runtime
    // FIX M2: Counters usan MarkDirty (save diferido) en vez de SaveGroups
    // ========================================================================
    bool CanDeploy(string groupID)
    {
        if (!m_Groups.Contains(groupID))
            return false;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return false;

        int limit = group.GetDeployLimit(m_Config);
        return (group.m_DeployedCount < limit);
    }

    void IncrementDeployCount(string groupID)
    {
        if (!m_Groups.Contains(groupID))
            return;
        LFPG_GroupData group = m_Groups.Get(groupID);
        if (group)
        {
            group.m_DeployedCount = group.m_DeployedCount + 1;
            // FIX M-6: Debug log en vez de PrintToRPT para reducir spam
            string incMsg = "IncrementDeploy: group=";
            incMsg = incMsg + groupID;
            incMsg = incMsg + " newCount=";
            incMsg = incMsg + group.m_DeployedCount.ToString();
            LFPG_Log.Debug(incMsg);
            MarkDirty();
            SendCounterSyncToMembers(group);
        }
    }

    void DecrementDeployCount(string groupID)
    {
        if (!m_Groups.Contains(groupID))
            return;
        LFPG_GroupData group = m_Groups.Get(groupID);
        if (group && group.m_DeployedCount > 0)
        {
            group.m_DeployedCount = group.m_DeployedCount - 1;
            MarkDirty();
            SendCounterSyncToMembers(group);
        }
    }

    bool CanPlaceGarden(string groupID)
    {
        if (!m_Groups.Contains(groupID))
            return false;
        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return false;
        return (group.m_GardenPlotCount < m_Config.m_MaxGardenPlotsPerFlag);
    }

    void IncrementGardenCount(string groupID)
    {
        if (!m_Groups.Contains(groupID))
            return;
        LFPG_GroupData group = m_Groups.Get(groupID);
        if (group)
        {
            group.m_GardenPlotCount = group.m_GardenPlotCount + 1;
            MarkDirty();
            SendCounterSyncToMembers(group);
        }
    }

    void DecrementGardenCount(string groupID)
    {
        if (!m_Groups.Contains(groupID))
            return;
        LFPG_GroupData group = m_Groups.Get(groupID);
        if (group && group.m_GardenPlotCount > 0)
        {
            group.m_GardenPlotCount = group.m_GardenPlotCount - 1;
            MarkDirty();
            SendCounterSyncToMembers(group);
        }
    }

    // FIX M2: Marcar como dirty (save diferido en siguiente ValidationTick)
    void MarkDirty()
    {
        m_IsDirty = true;
    }

    // FIX G-3 + D-8: Recalibrate por grupo (en vez de global O(N x m))
    // Se llama:
    //   - al arranque una vez (primer tick post-load)
    //   - on-demand via QueueRecalibrate(groupID) cuando el tracker este sospechoso
    //   - como fallback integrity check cada m_RecalibrationIntervalSeconds (default 1800s=30min)
    protected void RecalibrateGroup(string groupID)
    {
        if (!m_Groups.Contains(groupID))
            return;

        LFPG_GroupData group = m_Groups.Get(groupID);
        LFPG_FlagBase flag = GetGroupFlag(groupID);
        if (!group || !flag)
            return;

        vector pos = flag.GetPosition();
        float radius = m_Config.m_BuildRadiusMeters;

        m_RecalObjectBuffer.Clear();
        m_RecalCargoBuffer.Clear();
        GetGame().GetObjectsAtPosition(pos, radius, m_RecalObjectBuffer, m_RecalCargoBuffer);

        int deployCount = 0;
        int gardenCount = 0;
        bool enablePlots = m_Config.m_EnablePlots;

        // Limpiar SOLO las entries de este grupo (no global)
        LFPG_DeployTracker.ClearByGroup(groupID);

        if (!group.m_DeployedItemNames)
            group.m_DeployedItemNames = new array<string>;
        if (!group.m_GardenItemNames)
            group.m_GardenItemNames = new array<string>;
        group.m_DeployedItemNames.Clear();
        group.m_GardenItemNames.Clear();

        int objCount = m_RecalObjectBuffer.Count();
        int j;
        for (j = 0; j < objCount; j = j + 1)
        {
            Object obj = m_RecalObjectBuffer[j];
            if (!obj)
                continue;

            EntityAI ent = EntityAI.Cast(obj);
            if (!ent)
                continue;

            if (GardenPlot.Cast(obj))
            {
                string gardenName = ent.GetDisplayName();
                if (enablePlots)
                {
                    gardenCount = gardenCount + 1;
                    LFPG_DeployTracker.TrackGarden(ent, groupID);
                    group.m_GardenItemNames.Insert(gardenName);
                }
                else
                {
                    deployCount = deployCount + 1;
                    LFPG_DeployTracker.Track(ent, groupID);
                    group.m_DeployedItemNames.Insert(gardenName);
                }
                continue;
            }

            if (LFPG_IsExcludedFromDeploy(ent))
                continue;

            if (m_Config && m_Config.IsTypeExcludedFromFurniture(ent))
                continue;

            if (m_Config && m_Config.IsGreenhouse(ent))
            {
                string ghName = ent.GetDisplayName();
                gardenCount = gardenCount + 1;
                LFPG_DeployTracker.TrackGarden(ent, groupID);
                group.m_GardenItemNames.Insert(ghName);
                continue;
            }

            if (LFPG_CountsAsFurniture(ent, false))
            {
                deployCount = deployCount + 1;
                LFPG_DeployTracker.Track(ent, groupID);
                string furnName = ent.GetDisplayName();
                group.m_DeployedItemNames.Insert(furnName);
            }
        }

        bool changed = (group.m_DeployedCount != deployCount || group.m_GardenPlotCount != gardenCount);
        if (changed)
        {
            string calMsg = "Counter recal for ";
            calMsg = calMsg + groupID;
            calMsg = calMsg + ": deploy ";
            calMsg = calMsg + group.m_DeployedCount.ToString();
            calMsg = calMsg + "->";
            calMsg = calMsg + deployCount.ToString();
            calMsg = calMsg + ", garden ";
            calMsg = calMsg + group.m_GardenPlotCount.ToString();
            calMsg = calMsg + "->";
            calMsg = calMsg + gardenCount.ToString();
            LFPG_Log.Debug(calMsg);

            group.m_DeployedCount = deployCount;
            group.m_GardenPlotCount = gardenCount;
            MarkDirty();
            SendCounterSyncToMembers(group);
        }
    }

    // Recalibrate global de fallback (integrity check lento - cada 30min por defecto)
    // y post-startup (1er tick).
    protected void RecalibrateCounters()
    {
        int groupCount = m_Groups.Count();
        int i;
        for (i = 0; i < groupCount; i = i + 1)
        {
            string groupID = m_Groups.GetKey(i);
            RecalibrateGroup(groupID);
        }
        LFPG_Log.Info("Counter recalibration (all groups) complete.");
    }

    // FIX G-3: Queue on-demand para un solo grupo (invocado al crear/dissolver/upgrade)
    void QueueRecalibrate(string groupID)
    {
        if (groupID == "")
            return;
        RecalibrateGroup(groupID);
    }

    // ========================================================================
    // GROUP CRUD OPERATIONS
    // ========================================================================

    // Crear grupo: jugador sin grupo coloca bandera
    string CreateGroup(string playerUID, string playerName, string groupName, LFPG_FlagBase flag)
    {
        // Validaciones
        if (m_PlayerToGroup.Contains(playerUID))
            return "";

        if (m_GroupNames.Contains(groupName))
            return "";

        if (!flag)
            return "";

        // Crear ID
        string groupID = LFPG_GroupData.GenerateGroupID(playerUID);

        // Crear datos del grupo
        LFPG_GroupData group = new LFPG_GroupData();
        group.m_GroupID = groupID;
        group.m_GroupName = groupName;
        group.m_LeaderUID = playerUID;
        group.m_Tier = flag.GetTier();
        group.m_DeployedCount = 0;
        group.m_GardenPlotCount = 0;

        // Anadir lider como primer miembro
        LFPG_MemberData member = new LFPG_MemberData();
        member.Set(playerUID, playerName, GetGame().GetTime());
        group.m_Members.Insert(member);

        // NetworkID de la bandera
        int netLow = 0;
        int netHigh = 0;
        flag.GetNetworkID(netLow, netHigh);
        group.m_FlagNetLow = netLow;
        group.m_FlagNetHigh = netHigh;

        // Registrar en estructuras
        m_Groups.Set(groupID, group);
        m_PlayerToGroup.Set(playerUID, groupID);
        // FIX C-5: No anadir nombres temporales al set de unicidad
        // (se normalizaran cuando el lider escoja un nombre real)
        if (!IsTempGroupName(groupName))
        {
            m_GroupNames.Set(groupName, true);
        }

        // Configurar la bandera
        flag.SetGroupID(groupID);
        flag.SetMemberCount(1);
        group.m_FlagPosition = flag.GetPosition();
        if (!RegisterFlag(flag, groupID))
        {
            string regErrCreate = "CreateGroup: flag register rejected for group ";
            regErrCreate = regErrCreate + groupID;
            LFPG_Log.Error(regErrCreate);
        }

        // Establecer SyncVar de acciones habilitadas segun config del tier
        flag.InitFlagActionsEnabled();

        // Auto-raise: toda bandera recien creada empieza completamente subida.
        // Decae con el tiempo segun la duracion del tier.
        flag.SetFullyRaised();

        // Persistir
        SaveGroups();

        string logMsg = "Group created: ";
        logMsg = logMsg + groupName;
        logMsg = logMsg + " (";
        logMsg = logMsg + groupID;
        logMsg = logMsg + ") by ";
        logMsg = logMsg + playerUID;
        LFPG_Log.Info(logMsg);

        return groupID;
    }

    // Disolver grupo: se llama al destruir bandera o grupo huerfano
    void DissolveGroup(string groupID)
    {
        // Durante el apagado el estado final ya se guardo: ni mutar ni persistir.
        if (m_IsShuttingDown)
        {
            string shutDis = "DissolveGroup skipped, shutting down: ";
            shutDis = shutDis + groupID;
            LFPG_Log.Debug(shutDis);
            return;
        }

        if (!m_Groups.Contains(groupID))
            return;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return;

        // Limpiar m_PlayerToGroup para todos los miembros
        int i;
        int memberCount = group.m_Members.Count();
        for (i = 0; i < memberCount; i = i + 1)
        {
            LFPG_MemberData member = group.m_Members[i];
            if (member)
            {
                if (m_PlayerToGroup.Contains(member.m_PlayerUID))
                {
                    m_PlayerToGroup.Remove(member.m_PlayerUID);
                }

                // Notificar a cada miembro online
                SendGroupDissolved(member.m_PlayerUID, groupID);
            }
        }

        // Limpiar nombre (solo si no es temporal — los temp nunca entraron al set)
        if (!IsTempGroupName(group.m_GroupName) && m_GroupNames.Contains(group.m_GroupName))
        {
            m_GroupNames.Remove(group.m_GroupName);
        }

        // Destruir objetos desplegados si la config lo indica
        // ANTES de UnregisterFlag - DestroyDeployedObjects usa GetGroupFlag()
        if (m_Config && m_Config.m_DestroyDeployedOnDissolve)
        {
            DestroyDeployedObjects(group);
        }

        // FIX I-23: Limpiar las entries del tracker para este grupo
        // (si m_DestroyDeployedOnDissolve=false, los objetos sobreviven pero el tracker
        // queda apuntando a un grupo disuelto; sin Clear quedarian como huerfanos)
        LFPG_DeployTracker.ClearByGroup(groupID);

        // Limpiar flag references (DESPUES de destruir deployed objects)
        UnregisterFlag(groupID);

        // Eliminar grupo
        m_Groups.Remove(groupID);

        SaveGroups();

        string logMsg = "Group dissolved: ";
        logMsg = logMsg + groupID;
        LFPG_Log.Info(logMsg);
    }

    // Anadir miembro
    bool AddMember(string groupID, string playerUID, string playerName)
    {
        if (!m_Groups.Contains(groupID))
            return false;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return false;

        if (group.GetMemberCount() >= m_Config.m_MaxGroupSize)
            return false;

        if (group.IsMember(playerUID))
            return false;

        if (m_PlayerToGroup.Contains(playerUID))
            return false;

        // FIX G-16: Double-check que el grupo sigue existiendo tras los guards anteriores
        // (race entre HandleRequestJoin y DissolveGroup)
        if (!m_Groups.Contains(groupID))
            return false;

        LFPG_MemberData member = new LFPG_MemberData();
        member.Set(playerUID, playerName, GetGame().GetTime());
        group.m_Members.Insert(member);

        m_PlayerToGroup.Set(playerUID, groupID);

        // Actualizar SyncVar en la bandera
        LFPG_FlagBase flag = GetGroupFlag(groupID);
        if (flag)
        {
            flag.SetMemberCount(group.GetMemberCount());

            // FIX AUDIT: Desactivar invite mode si el grupo llego a capacidad maxima
            if (group.GetMemberCount() >= m_Config.m_MaxGroupSize)
            {
                flag.DeactivateInviteMode();
            }
        }

        SaveGroups();

        // Notificar a todos los miembros online
        SendGroupSyncUpdateToMembers(group, LFPG_SYNC_MEMBER_ADDED);

        return true;
    }

    // Quitar miembro - con traspaso de liderazgo si es lider que abandona
    bool RemoveMember(string groupID, string playerUID)
    {
        if (!m_Groups.Contains(groupID))
            return false;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return false;

        int memberIdx = group.FindMemberIndex(playerUID);
        if (memberIdx < 0)
            return false;

        bool wasLeader = group.IsLeader(playerUID);

        // Quitar del array de miembros
        group.m_Members.Remove(memberIdx);

        // Quitar del lookup rapido
        if (m_PlayerToGroup.Contains(playerUID))
        {
            m_PlayerToGroup.Remove(playerUID);
        }

        // Si era el lider Y quedan miembros -> traspasar liderazgo
        if (wasLeader && group.GetMemberCount() > 0)
        {
            string newLeaderUID = group.GetOldestMemberUID();
            if (newLeaderUID != "")
            {
                group.m_LeaderUID = newLeaderUID;
            }
            else
            {
                // Fallback: primer miembro
                LFPG_MemberData first = group.m_Members[0];
                if (first)
                {
                    group.m_LeaderUID = first.m_PlayerUID;
                }
            }
        }

        // Si no quedan miembros -> manejar abandono
        if (group.GetMemberCount() <= 0)
        {
            // Capturar referencia a la bandera ANTES de disolver
            // (DissolveGroup llama UnregisterFlag que limpia m_GroupFlags)
            LFPG_FlagBase abandonedFlag = GetGroupFlag(groupID);
            vector abandonedPos = vector.Zero;
            int abandonedTier = 1;
            if (abandonedFlag)
            {
                abandonedPos = abandonedFlag.GetPosition();
                abandonedTier = abandonedFlag.GetTier();
            }

            SendGroupDissolved(playerUID, groupID);
            DissolveGroup(groupID);

            // FIX BUG2: Limpiar bandera abandonada y re-insertar en cache
            // La bandera sigue en el mundo pero sin grupo ni miembros
            if (abandonedFlag)
            {
                abandonedFlag.SetGroupID("");
                abandonedFlag.SetMemberCount(0);

                // FIX AUDIT: Desactivar invite mode si estaba activo
                // Previene que otros jugadores vean "Join Group" en bandera sin dueno
                abandonedFlag.DeactivateInviteMode();

                // FIX AUDIT: Resetear raise progress - la bandera baja visualmente
                // y la zona de construccion fantasma se desactiva
                abandonedFlag.ResetRaiseProgress();

                // Re-insertar posicion como ABANDONED para que IsPositionInTerritory
                // siga bloqueando colocacion de nuevas banderas cerca
                UpdateFlagPositionCache(LFPG_ABANDONED_GROUP, abandonedPos, 0.0, abandonedTier);

                string abandonMsg = "Flag abandoned at ";
                abandonMsg = abandonMsg + abandonedPos.ToString();
                LFPG_Log.Info(abandonMsg);
            }

            return true;
        }

        // Actualizar SyncVar
        LFPG_FlagBase flag = GetGroupFlag(groupID);
        if (flag)
        {
            flag.SetMemberCount(group.GetMemberCount());
        }

        SaveGroups();

        // Notificar al que se fue
        SendGroupDissolved(playerUID, groupID);

        // Notificar al resto
        SendGroupSyncUpdateToMembers(group, LFPG_SYNC_MEMBER_REMOVED);

        return true;
    }

    // Transferir liderazgo (solo por peticion voluntaria del lider)
    bool TransferLeadership(string groupID, string currentLeaderUID, string newLeaderUID)
    {
        if (!m_Groups.Contains(groupID))
            return false;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return false;

        if (!group.IsLeader(currentLeaderUID))
            return false;

        if (!group.IsMember(newLeaderUID))
            return false;

        group.m_LeaderUID = newLeaderUID;
        SaveGroups();

        SendGroupSyncUpdateToMembers(group, LFPG_SYNC_LEADER_CHANGED);
        return true;
    }

    // FIX I-6: Helper de integrity — existe el grupo en memoria?
    bool GroupExists(string groupID)
    {
        return m_Groups.Contains(groupID);
    }

    // FIX C-5: Detecta nombres temporales #TEMP#*
    static bool IsTempGroupName(string name)
    {
        string prefix = "#TEMP#";
        int prefixLen = prefix.Length();
        int nameLen = name.Length();
        if (nameLen < prefixLen)
            return false;
        string start = name.Substring(0, prefixLen);
        return (start == prefix);
    }

    // Obtener grupo de un jugador
    LFPG_GroupData GetGroupByPlayer(string playerUID)
    {
        if (!m_PlayerToGroup.Contains(playerUID))
            return null;

        string groupID = m_PlayerToGroup.Get(playerUID);
        if (!m_Groups.Contains(groupID))
            return null;

        return m_Groups.Get(groupID);
    }

    // Obtener la bandera de un grupo
    LFPG_FlagBase GetGroupFlag(string groupID)
    {
        if (!m_GroupFlags.Contains(groupID))
            return null;
        return m_GroupFlags.Get(groupID);
    }

    // ?Tiene este jugador un grupo?
    bool HasGroup(string playerUID)
    {
        return m_PlayerToGroup.Contains(playerUID);
    }

    // Obtener groupID de un jugador
    string GetPlayerGroupID(string playerUID)
    {
        if (!m_PlayerToGroup.Contains(playerUID))
            return "";
        return m_PlayerToGroup.Get(playerUID);
    }

    // ========================================================================
    // STALE GROUP CLEANUP - Detecta y disuelve grupos cuya bandera ya no existe
    // Llamado defensivamente antes de operaciones criticas (colocar bandera)
    // Si EEDelete fallo por cualquier razon, esto lo atrapa
    // ========================================================================
    bool CleanupStaleGroupForPlayer(string playerUID)
    {
        // Con la red de seguridad activa nadie borra grupos, ni siquiera por
        // la via del kit: el estado de arranque no es de fiar.
        if (m_DissolveDisabled)
            return false;

        if (!HasGroup(playerUID))
            return false;

        string groupID = GetPlayerGroupID(playerUID);
        if (groupID == "")
            return false;

        LFPG_FlagBase flag = GetGroupFlag(groupID);
        if (flag)
            return false;

        // Flag entity is gone but group persists — stale group
        string logMsg = "Stale group detected (flag destroyed), dissolving: ";
        logMsg = logMsg + groupID;
        logMsg = logMsg + " for player: ";
        logMsg = logMsg + playerUID;
        LFPG_Log.Error(logMsg);
        DissolveGroup(groupID);
        return true;
    }

    // ========================================================================
    // UPGRADE - Swap de entidad de bandera
    // ========================================================================
    bool UpgradeFlag(string groupID, string newClassName, LFPG_FlagBase oldFlag)
    {
        if (!oldFlag || groupID == "")
            return false;

        if (!m_Groups.Contains(groupID))
            return false;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return false;

        // Guardar datos de la vieja bandera
        vector pos = oldFlag.GetPosition();
        vector ori = oldFlag.GetOrientation();
        int memberCount = oldFlag.GetMemberCount();

        // Spawn nueva bandera en la posicion EXACTA de la vieja
        // ECE_CREATEPHYSICS preserva posicion exacta (ECE_PLACE_ON_SURFACE puede mover Y)
        Object obj = GetGame().CreateObjectEx(newClassName, pos, ECE_CREATEPHYSICS);
        LFPG_FlagBase newFlag = LFPG_FlagBase.Cast(obj);
        if (!newFlag)
        {
            // ABORT - no borrar vieja, no modificar nada
            string errMsg = "Failed to spawn upgrade entity: ";
            errMsg = errMsg + newClassName;
            LFPG_Log.Error(errMsg);
            return false;
        }

        // Configurar nueva bandera - posicion y orientacion EXACTAS
        newFlag.SetPosition(pos);
        newFlag.SetOrientation(ori);
        newFlag.TransferDataFrom(oldFlag);
        newFlag.SetMemberCount(memberCount);

        // Actualizar tier en grupo
        group.m_Tier = newFlag.GetTier();

        // FIX M-4: Actualizar FlagPosition persistida (aunque la posicion no cambia,
        // reafirmamos la coherencia por si el SetPosition sufrio drift minimo)
        group.m_FlagPosition = newFlag.GetPosition();

        // Actualizar NetworkID
        int netLow = 0;
        int netHigh = 0;
        newFlag.GetNetworkID(netLow, netHigh);
        group.m_FlagNetLow = netLow;
        group.m_FlagNetHigh = netHigh;

        // Finalizar estado fisico de la nueva bandera
        newFlag.Update();

        // FIX I-11: Re-anchor de pathgraph tras upgrade (defensivo si el terreno cambio)
        // UpdatePathgraphRegionByObject vive en CGame, no en la entidad — se pasa la entidad como param4 de CallLater.
        newFlag.SetAffectPathgraph(true, false);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(GetGame().UpdatePathgraphRegionByObject, 100, false, newFlag);

        // Registrar nueva bandera, desregistrar vieja
        UnregisterFlag(groupID);
        if (!RegisterFlag(newFlag, groupID))
        {
            string regErrUpgrade = "UpgradeFlag: flag register rejected for group ";
            regErrUpgrade = regErrUpgrade + groupID;
            LFPG_Log.Error(regErrUpgrade);
        }

        // FIX C-8: Cancelar CallLater huerfano de DeactivateInviteMode en la vieja
        // antes del delete + resetear invite state en la nueva.
        oldFlag.DeactivateInviteMode();
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(oldFlag.DeactivateInviteMode);

        // Borrar entidad vieja (DESPUES de registrar la nueva)
        // FIX: Desactivar dissolve en EEDelete — la vieja bandera ya no es duena del grupo
        oldFlag.SetSkipDissolveOnDelete();
        GetGame().ObjectDelete(oldFlag);

        // FIX M-4: Persistir inmediatamente (no diferir con MarkDirty: un restart
        // en la ventana de 60s del tick perderia el cambio de tier)
        SaveGroups();

        // Notificar miembros
        SendGroupSyncUpdateToMembers(group, LFPG_SYNC_TIER_CHANGED);

        string logMsg = "Flag upgraded: group=";
        logMsg = logMsg + groupID;
        logMsg = logMsg + " newTier=";
        logMsg = logMsg + newFlag.GetTier().ToString();
        LFPG_Log.Info(logMsg);

        return true;
    }

    // ========================================================================
    // NAME VALIDATION
    // ========================================================================
    int ValidateGroupName(string name)
    {
        int nameLen = name.Length();
        if (nameLen < m_Config.m_GroupNameMinLength)
            return LFPG_NAME_TOO_SHORT;

        if (nameLen > m_Config.m_GroupNameMaxLength)
            return LFPG_NAME_TOO_LONG;

        // Check caracteres permitidos
        int i;
        for (i = 0; i < nameLen; i = i + 1)
        {
            string ch = name[i];
            int charIdx = LFPG_NAME_ALLOWED_CHARS.IndexOf(ch);
            if (charIdx < 0)
                return LFPG_NAME_INVALID_CHARS;
        }

        // Check nombre duplicado - O(1) via map
        if (m_GroupNames.Contains(name))
            return LFPG_NAME_TAKEN;

        return LFPG_NAME_OK;
    }

    // ========================================================================
    // RPC RATE LIMITING - Anti-spam
    // FIX M-2: Throttle configurable via m_Config.m_RpcThrottleMs
    // ========================================================================
    protected bool IsRPCThrottled(string playerUID)
    {
        int now = GetGame().GetTime();
        int throttleMs = 500;
        if (m_Config && m_Config.m_RpcThrottleMs > 0)
            throttleMs = m_Config.m_RpcThrottleMs;

        if (m_RPCThrottle.Contains(playerUID))
        {
            int lastTime = m_RPCThrottle.Get(playerUID);
            int diff = now - lastTime;
            if (diff < throttleMs)
                return true;
        }
        m_RPCThrottle.Set(playerUID, now);
        return false;
    }

    // ========================================================================
    // FIX C-7, G-15: Validacion de propiedad + distancia para RPCs in-situ
    // ========================================================================
    protected bool ValidateSenderOwnsFlag(PlayerIdentity sender, LFPG_FlagBase flag)
    {
        if (!sender || !flag)
            return false;
        string senderUID = sender.GetPlainId();
        string senderGroup = GetPlayerGroupID(senderUID);
        if (senderGroup == "")
            return false;
        if (flag.GetGroupID() != senderGroup)
            return false;
        return true;
    }

    protected bool IsSenderNearFlag(PlayerIdentity sender, LFPG_FlagBase flag, float maxDist)
    {
        if (!sender || !flag)
            return false;
        Man man = sender.GetPlayer();
        if (!man)
            return false;
        float distSq = vector.DistanceSq(man.GetPosition(), flag.GetPosition());
        return (distSq <= maxDist * maxDist);
    }

    // ========================================================================
    // RPC HANDLER - Server-side dispatcher
    // ========================================================================
    void HandleRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        if (!sender)
            return;

        string senderUID = sender.GetPlainId();
        string senderName = sender.GetName();

        // Rate limiting
        if (IsRPCThrottled(senderUID))
            return;

        if (rpc_type == LFPG_RPC_C2S_CREATE_GROUP)
        {
            // RESERVED — no triggered from client (group created via Action/OnPlacementComplete)
            HandleCreateGroup(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_SET_GROUP_NAME)
        {
            HandleSetGroupName(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_REQUEST_JOIN)
        {
            // RESERVED — no triggered from client (join via ActionJoinGroup.OnStartServer)
            HandleRequestJoin(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_REQUEST_LEAVE)
        {
            HandleRequestLeave(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_REQUEST_KICK)
        {
            HandleRequestKick(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_REQUEST_TRANSFER)
        {
            HandleRequestTransfer(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_START_INVITE)
        {
            // RESERVED — no triggered from client (invite via ActionInvite.OnStartServer)
            HandleStartInvite(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_DESTROY_FLAG)
        {
            // RESERVED — no triggered from client (destroy via ActionDestroyFlag.OnStartServer)
            HandleDestroyFlag(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_REQUEST_GROUP_DATA)
        {
            HandleRequestGroupData(sender, ctx, flag);
        }
    }

    // ========================================================================
    // RPC HANDLERS - Individual operations
    // ========================================================================

    // FIX G-14: Marcado como DEPRECATED — no se dispara desde cliente.
    // La creacion de grupo ocurre via LFPG_FlagKit_T1.OnPlacementComplete o
    // LFPG_ActionRegisterTerritory.OnStartServer. Este handler se rechaza
    // para evitar doble-register race.
    protected void HandleCreateGroup(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        LFPG_Log.Error("HandleCreateGroup called - rejected (use ActionRegisterTerritory or FlagKit placement instead)");
    }

    protected void HandleSetGroupName(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string senderUID = sender.GetPlainId();

        string newName = "";
        if (!ctx.Read(newName))
            return;

        string groupID = GetPlayerGroupID(senderUID);
        if (groupID == "")
            return;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return;

        // Solo el lider puede renombrar
        if (!group.IsLeader(senderUID))
            return;

        // Validar nombre
        int result = ValidateGroupName(newName);
        if (result != LFPG_NAME_OK)
        {
            SendNameResult(sender, flag, result);
            return;
        }

        // Quitar nombre viejo del set (si no era temporal — los temp nunca entraron)
        if (!IsTempGroupName(group.m_GroupName) && m_GroupNames.Contains(group.m_GroupName))
        {
            m_GroupNames.Remove(group.m_GroupName);
        }

        // Asignar nuevo nombre (el nombre validado no puede ser temp — LFPG_NAME_ALLOWED_CHARS no incluye '#')
        group.m_GroupName = newName;
        m_GroupNames.Set(newName, true);

        SaveGroups();

        SendNameResult(sender, flag, LFPG_NAME_OK);
        // Notificar a todos con sync completo (nombre cambio)
        SendGroupSyncUpdateToMembers(group, LFPG_SYNC_COUNT_CHANGED);
    }

    protected void HandleRequestJoin(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string senderUID = sender.GetPlainId();
        string senderName = sender.GetName();

        // No debe tener grupo
        if (HasGroup(senderUID))
            return;

        // La bandera debe estar en invite mode
        if (!flag || !flag.IsInviteModeActive())
            return;

        // FIX C-7: Distance check (proximidad requerida para unirse)
        if (!IsSenderNearFlag(sender, flag, 10.0))
            return;

        string groupID = flag.GetGroupID();
        if (groupID == "")
            return;

        bool added = AddMember(groupID, senderUID, senderName);
        if (added)
        {
            SendGroupSyncFull(sender, groupID, flag, flag);
        }
    }

    protected void HandleRequestLeave(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string senderUID = sender.GetPlainId();
        string groupID = GetPlayerGroupID(senderUID);
        if (groupID == "")
            return;

        RemoveMember(groupID, senderUID);
    }

    protected void HandleRequestKick(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string senderUID = sender.GetPlainId();
        string groupID = GetPlayerGroupID(senderUID);
        if (groupID == "")
            return;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group || !group.IsLeader(senderUID))
            return;

        string targetUID = "";
        if (!ctx.Read(targetUID))
            return;

        // No puedes kickearte a ti mismo
        if (targetUID == senderUID)
            return;

        RemoveMember(groupID, targetUID);
    }

    protected void HandleRequestTransfer(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string senderUID = sender.GetPlainId();
        string groupID = GetPlayerGroupID(senderUID);
        if (groupID == "")
            return;

        string targetUID = "";
        if (!ctx.Read(targetUID))
            return;

        TransferLeadership(groupID, senderUID, targetUID);
    }

    protected void HandleStartInvite(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        if (!flag)
            return;

        // FIX C-7, G-15: Validar ownership + proximidad (10m)
        if (!ValidateSenderOwnsFlag(sender, flag))
            return;
        if (!IsSenderNearFlag(sender, flag, 10.0))
            return;

        // Activar invite mode
        int durationMs = m_Config.m_InviteDurationSeconds * 1000;
        flag.ActivateInviteMode(durationMs);
    }

    protected void HandleDestroyFlag(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        if (!flag)
            return;

        // FIX C-7, G-15: Validar ownership + proximidad (10m)
        if (!ValidateSenderOwnsFlag(sender, flag))
            return;
        if (!IsSenderNearFlag(sender, flag, 10.0))
            return;

        string senderUID = sender.GetPlainId();
        string groupID = flag.GetGroupID();

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group || !group.IsLeader(senderUID))
            return;

        // Verificar que el jugador tiene hatchet en manos (LFPG_ToolMatcher en Fase F)
        PlayerBase player = PlayerBase.Cast(sender.GetPlayer());
        if (!player)
            return;

        EntityAI itemInHands = player.GetHumanInventory().GetEntityInHands();
        if (!itemInHands)
            return;

        if (!LFPG_IsHatchet(itemInHands))
            return;

        // Disolver grupo y destruir bandera
        DissolveGroup(groupID);

        // Borrar la entidad de la bandera del mundo
        GetGame().ObjectDelete(flag);
    }

    protected void HandleRequestGroupData(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string senderUID = sender.GetPlainId();
        string groupID = GetPlayerGroupID(senderUID);
        if (groupID == "")
            return;

        // Si flag es null (RPC llego via PlayerBase), buscar la flag del grupo
        if (!flag)
        {
            flag = GetGroupFlag(groupID);
        }

        // Resolver player como fallback de rpcTarget cuando flag no esta disponible
        // (fuera de network bubble o aun no cargada tras restart)
        PlayerBase senderPlayer = GetPlayerByUID(senderUID);
        Object sendVia = flag;
        if (!sendVia && senderPlayer)
        {
            sendVia = senderPlayer;
        }

        if (!sendVia)
            return;

        SendGroupSyncFull(sender, groupID, flag, sendVia);
    }

    // ========================================================================
    // PLAYER JOIN/LEAVE SERVER - Llamado desde modded PlayerBase
    // ========================================================================
    void OnPlayerJoined(string playerUID, PlayerBase player)
    {
        if (!HasGroup(playerUID))
            return;

        string groupID = GetPlayerGroupID(playerUID);
        LFPG_FlagBase flag = GetGroupFlag(groupID);

        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return;

        // FIX RECONNECT: Enviar FULL sync via PlayerBase (siempre existe en client).
        // LightweightSync no incluye datos de miembros -> lista vacia en UI.
        // PlayerBase como rpcTarget garantiza entrega incluso si flag fuera de network bubble.
        SendGroupSyncFull(identity, groupID, flag, player);

        // Si el grupo tiene nombre temporal y este jugador es lider,
        // re-enviar dialogo de nombre (forzado)
        // Prefijo temporal "#TEMP#" - contiene '#' que no esta en
        // LFPG_NAME_ALLOWED_CHARS, imposible que un jugador lo escriba
        if (m_Groups.Contains(groupID))
        {
            LFPG_GroupData group = m_Groups.Get(groupID);
            if (group && group.IsLeader(playerUID))
            {
                string prefix = "#TEMP#";
                int prefixLen = prefix.Length();
                int nameLen = group.m_GroupName.Length();
                if (nameLen >= prefixLen)
                {
                    string nameStart = group.m_GroupName.Substring(0, prefixLen);
                    if (nameStart == prefix)
                    {
                        if (flag)
                        {
                            SendOpenNameDialog(identity, flag, groupID);
                        }
                    }
                }
            }
        }
    }

    // ========================================================================
    // SEND RPC HELPERS - Server -> Client
    // ========================================================================

    // rpcTarget: entidad via la que se envia el RPC al cliente.
    // Normalmente es flag (cuando el jugador esta cerca y la flag esta en su network bubble).
    // Para reconnect o RequestGroupData con flag fuera de bubble, pasar PlayerBase.
    void SendGroupSyncFull(PlayerIdentity target, string groupID, LFPG_FlagBase flag, Object rpcTarget)
    {
        if (!target || !rpcTarget)
            return;

        if (!m_Groups.Contains(groupID))
            return;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(group.m_GroupID);
        rpc.Write(group.m_GroupName);
        rpc.Write(group.m_LeaderUID);
        rpc.Write(group.m_Tier);
        rpc.Write(group.m_DeployedCount);
        rpc.Write(group.m_GardenPlotCount);

        // Miembros + online status
        int memberCount = group.GetMemberCount();
        rpc.Write(memberCount);

        // Un solo GetPlayers para resolver online status de todos los miembros
        m_PlayerSearchBuffer.Clear();
        GetGame().GetPlayers(m_PlayerSearchBuffer);
        int playerCount = m_PlayerSearchBuffer.Count();

        int i;
        for (i = 0; i < memberCount; i = i + 1)
        {
            LFPG_MemberData member = group.m_Members[i];
            if (member)
            {
                rpc.Write(member.m_PlayerUID);
                rpc.Write(member.m_PlayerName);

                // Check online contra el buffer local
                bool isOnline = false;
                int p;
                for (p = 0; p < playerCount; p = p + 1)
                {
                    Man man = m_PlayerSearchBuffer[p];
                    if (man)
                    {
                        PlayerIdentity pid = man.GetIdentity();
                        if (pid && pid.GetPlainId() == member.m_PlayerUID)
                        {
                            isOnline = true;
                            break;
                        }
                    }
                }
                rpc.Write(isOnline);
            }
        }

        // Deploy limit para este tier
        int deployLimit = group.GetDeployLimit(m_Config);
        rpc.Write(deployLimit);

        // Posicion de la bandera (para cache del cliente)
        // Fallback a posicion persistida si flag es null (fuera de network bubble)
        vector flagPos = vector.Zero;
        if (flag)
        {
            flagPos = flag.GetPosition();
        }
        else
        {
            flagPos = group.m_FlagPosition;
        }
        rpc.Write(flagPos);

        // Config values sincronizados (m_Config garantizado por Init)
        rpc.Write(m_Config.m_BuildRadiusSq);
        rpc.Write(m_Config.m_MaxGardenPlotsPerFlag);
        rpc.Write(m_Config.m_MaxGroupSize);

        // Config flags para client
        rpc.Write(m_Config.m_EnablePlots);
        rpc.Write(m_Config.m_EnableGreenhouseAsPlot);

        // Greenhouse whitelist (para hologram client-side)
        int ghCount = 0;
        if (m_Config.m_GreenhouseWhitelist)
        {
            ghCount = m_Config.m_GreenhouseWhitelist.Count();
        }
        rpc.Write(ghCount);
        int gh;
        for (gh = 0; gh < ghCount; gh = gh + 1)
        {
            rpc.Write(m_Config.m_GreenhouseWhitelist[gh]);
        }

        // v3+: NoBaseRequired types (lista A) - placement sin grupo pero no en ajeno
        int nbrCount = 0;
        if (m_Config.m_NoBaseRequiredTypes)
        {
            nbrCount = m_Config.m_NoBaseRequiredTypes.Count();
        }
        rpc.Write(nbrCount);
        int nbrI;
        for (nbrI = 0; nbrI < nbrCount; nbrI = nbrI + 1)
        {
            rpc.Write(m_Config.m_NoBaseRequiredTypes[nbrI]);
        }

        // v3+: Unrestricted types (lista B) - sin restriccion alguna
        int urCount = 0;
        if (m_Config.m_UnrestrictedTypes)
        {
            urCount = m_Config.m_UnrestrictedTypes.Count();
        }
        rpc.Write(urCount);
        int urI;
        for (urI = 0; urI < urCount; urI = urI + 1)
        {
            rpc.Write(m_Config.m_UnrestrictedTypes[urI]);
        }

        // FIX I-9: Truncar a max 32 items y nombres a 32 chars para limitar packet size
        // con grupos grandes. Tooltip muestra lo que quepa; admin puede ajustar deploy limit.
        int deployNameCount = 0;
        if (group.m_DeployedItemNames)
        {
            deployNameCount = group.m_DeployedItemNames.Count();
        }
        if (deployNameCount > 32)
            deployNameCount = 32;
        rpc.Write(deployNameCount);
        int dn;
        for (dn = 0; dn < deployNameCount; dn = dn + 1)
        {
            string dnFull = group.m_DeployedItemNames[dn];
            if (dnFull.Length() > 32)
                dnFull = dnFull.Substring(0, 32);
            rpc.Write(dnFull);
        }

        int gardenNameCount = 0;
        if (group.m_GardenItemNames)
        {
            gardenNameCount = group.m_GardenItemNames.Count();
        }
        if (gardenNameCount > 32)
            gardenNameCount = 32;
        rpc.Write(gardenNameCount);
        int gn;
        for (gn = 0; gn < gardenNameCount; gn = gn + 1)
        {
            string gnFull = group.m_GardenItemNames[gn];
            if (gnFull.Length() > 32)
                gnFull = gnFull.Substring(0, 32);
            rpc.Write(gnFull);
        }

        rpc.Send(rpcTarget, LFPG_RPC_S2C_GROUP_SYNC_FULL, true, target);
    }

    // Lightweight sync unificado - rpcTarget puede ser flag o PlayerBase
    // Usar flag cuando esta en network bubble, PlayerBase para reconnect (siempre disponible)
    void SendLightweightSync(PlayerIdentity target, string groupID, LFPG_FlagBase flag, Object rpcTarget)
    {
        if (!target || !rpcTarget)
            return;

        if (!m_Groups.Contains(groupID))
            return;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return;

        ScriptRPC rpc = new ScriptRPC();
        WriteLightweightSyncPayload(rpc, group, flag);
        rpc.Send(rpcTarget, LFPG_RPC_S2C_LIGHTWEIGHT_SYNC, true, target);
    }

    // Payload comun del lightweight sync (evita duplicacion)
    protected void WriteLightweightSyncPayload(ScriptRPC rpc, LFPG_GroupData group, LFPG_FlagBase flag)
    {
        rpc.Write(group.m_GroupID);
        rpc.Write(group.m_GroupName);
        rpc.Write(group.m_LeaderUID);
        rpc.Write(group.m_Tier);
        rpc.Write(group.m_DeployedCount);
        rpc.Write(group.GetDeployLimit(m_Config));

        if (flag)
        {
            rpc.Write(flag.GetPosition());
            rpc.Write(flag.ComputeCurrentRaiseProgress());
        }
        else
        {
            // Fallback: usar posicion persistida del grupo cuando la entidad flag
            // aun no ha cargado (e.g., reconnect temprano, flag fuera de network bubble)
            rpc.Write(group.m_FlagPosition);
            rpc.Write(0.0);
        }

        rpc.Write(m_Config.m_BuildRadiusSq);
        rpc.Write(group.m_GardenPlotCount);
        rpc.Write(m_Config.m_MaxGardenPlotsPerFlag);
        rpc.Write(group.GetMemberCount());
        rpc.Write(m_Config.m_MaxGroupSize);
        rpc.Write(m_Config.m_EnablePlots);
    }

    protected void SendGroupSyncUpdateToMembers(LFPG_GroupData group, int updateType)
    {
        if (!group)
            return;

        LFPG_FlagBase flag = GetGroupFlag(group.m_GroupID);

        // Una sola llamada a GetPlayers (antes se llamaba N veces, una por miembro)
        m_PlayerSearchBuffer.Clear();
        GetGame().GetPlayers(m_PlayerSearchBuffer);

        int memberCount = group.m_Members.Count();
        int playerCount = m_PlayerSearchBuffer.Count();
        int i;
        int j;

        for (i = 0; i < memberCount; i = i + 1)
        {
            LFPG_MemberData member = group.m_Members[i];
            if (!member)
                continue;

            for (j = 0; j < playerCount; j = j + 1)
            {
                Man man = m_PlayerSearchBuffer[j];
                if (!man)
                    continue;
                PlayerIdentity identity = man.GetIdentity();
                if (!identity)
                    continue;
                if (identity.GetPlainId() == member.m_PlayerUID)
                {
                    // Enviar via flag si existe, sino via PlayerBase (man)
                    Object syncTarget = flag;
                    if (!syncTarget)
                        syncTarget = man;
                    SendGroupSyncFull(identity, group.m_GroupID, flag, syncTarget);
                    break;
                }
            }
        }
    }

    // FIX C: Enviar lightweight sync de counters a todos los miembros online
    // Llamado tras cada Increment/Decrement de deploy o garden counters
    // Usa lightweight sync (mas ligero que full sync, no incluye lista de miembros)
    protected void SendCounterSyncToMembers(LFPG_GroupData group)
    {
        if (!group)
            return;

        LFPG_FlagBase flag = GetGroupFlag(group.m_GroupID);

        m_PlayerSearchBuffer.Clear();
        GetGame().GetPlayers(m_PlayerSearchBuffer);

        int memberCount = group.m_Members.Count();
        int playerCount = m_PlayerSearchBuffer.Count();
        int i;
        int j;

        for (i = 0; i < memberCount; i = i + 1)
        {
            LFPG_MemberData member = group.m_Members[i];
            if (!member)
                continue;

            for (j = 0; j < playerCount; j = j + 1)
            {
                Man man = m_PlayerSearchBuffer[j];
                if (!man)
                    continue;
                PlayerIdentity identity = man.GetIdentity();
                if (!identity)
                    continue;
                if (identity.GetPlainId() == member.m_PlayerUID)
                {
                    Object syncTarget = flag;
                    if (!syncTarget)
                        syncTarget = man;
                    SendLightweightSync(identity, group.m_GroupID, flag, syncTarget);
                    break;
                }
            }
        }
    }

    protected void SendGroupDissolved(string playerUID, string groupID)
    {
        PlayerBase player = GetPlayerByUID(playerUID);
        if (!player)
            return;

        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return;

        // Enviar via PlayerBase (siempre existe en client)
        // La flag puede estar siendo destruida o fuera del network bubble
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(groupID);
        rpc.Send(player, LFPG_RPC_S2C_GROUP_DISSOLVED, true, identity);
    }

    void SendOpenNameDialog(PlayerIdentity target, LFPG_FlagBase flag, string groupID)
    {
        if (!target || !flag)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(groupID);
        rpc.Send(flag, LFPG_RPC_S2C_OPEN_NAME_DIALOG, true, target);
    }

    protected void SendNameResult(PlayerIdentity target, LFPG_FlagBase flag, int result)
    {
        if (!target || !flag)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(result);
        rpc.Send(flag, LFPG_RPC_S2C_NAME_RESULT, true, target);
    }

    // FIX AUDIT: Helper para enviar error via PlayerBase (siempre disponible)
    void SendErrorToPlayer(PlayerIdentity target, PlayerBase player, string msg)
    {
        if (!target || !player)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(msg);
        rpc.Send(player, LFPG_RPC_S2C_ERROR_MSG, true, target);
    }

    // ========================================================================
    // UTILITY
    // ========================================================================

    protected PlayerBase GetPlayerByUID(string uid)
    {
        // FIX H5: Reutilizar buffer en vez de new array
        m_PlayerSearchBuffer.Clear();
        GetGame().GetPlayers(m_PlayerSearchBuffer);
        int i;
        int count = m_PlayerSearchBuffer.Count();
        for (i = 0; i < count; i = i + 1)
        {
            Man man = m_PlayerSearchBuffer[i];
            if (!man)
                continue;
            PlayerIdentity identity = man.GetIdentity();
            if (!identity)
                continue;
            if (identity.GetPlainId() == uid)
            {
                return PlayerBase.Cast(man);
            }
        }
        return null;
    }

    // FIX G-5: Destruir TODOS los deployables del grupo (BBB + GardenPlot + tracked items)
    // No solo BBB. Usa el tracker como fuente de verdad + scan proximity como backup.
    protected void DestroyDeployedObjects(LFPG_GroupData group)
    {
        LFPG_FlagBase flag = GetGroupFlag(group.m_GroupID);
        if (!flag)
            return;

        vector pos = flag.GetPosition();
        float radius = m_Config.m_BuildRadiusMeters;
        string ownerGroupID = group.m_GroupID;

        m_RecalObjectBuffer.Clear();
        m_RecalCargoBuffer.Clear();
        GetGame().GetObjectsAtPosition(pos, radius, m_RecalObjectBuffer, m_RecalCargoBuffer);

        int i;
        int count = m_RecalObjectBuffer.Count();
        for (i = 0; i < count; i = i + 1)
        {
            Object obj = m_RecalObjectBuffer[i];
            if (!obj)
                continue;

            EntityAI ent = EntityAI.Cast(obj);
            if (!ent)
                continue;

            // Excluir flags y items en listas A/B (no estan atados al territorio)
            if (ent.IsKindOf("LFPG_FlagBase"))
                continue;
            if (m_Config && (m_Config.IsNoBaseRequired(ent) || m_Config.IsUnrestricted(ent)))
                continue;

            // Verificar que pertenece a ESTE grupo (evita danyo colateral con bases cercanas)
            string objOwner = FindGroupIDAtPosition(ent.GetPosition());
            if (objOwner != ownerGroupID)
                continue;

            // BBB: paredes, torres, shelters
            if (BaseBuildingBase.Cast(obj))
            {
                GetGame().ObjectDelete(obj);
                continue;
            }

            // GardenPlot y derivados
            if (GardenPlot.Cast(obj))
            {
                GetGame().ObjectDelete(obj);
                continue;
            }

            // Items deployables (barrels, crates, mod storages)
            ItemBase ib = ItemBase.Cast(obj);
            if (ib && ib.IsDeployable())
            {
                // Doble check exclusions para no borrar items vanilla no-muebles
                if (LFPG_IsExcludedFromDeploy(ent))
                    continue;
                if (m_Config && m_Config.IsTypeExcludedFromFurniture(ent))
                    continue;
                GetGame().ObjectDelete(obj);
            }
        }
    }

    // ========================================================================
    // PERSISTENCE - JSON atomico
    // ========================================================================
    void SaveGroups()
    {
        LFPG_GroupsFileData fileData = new LFPG_GroupsFileData();
        fileData.m_Version = LFPG_GROUPS_FILE_VERSION;

        // Copiar grupos al array
        int i;
        int count = m_Groups.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_GroupData group = m_Groups.GetElement(i);
            if (group)
            {
                fileData.m_Groups.Insert(group);
            }
        }

        // Cuenta realmente serializada. El bucle inserta condicionalmente, asi que
        // esto puede ser < m_Groups.Count(); el verify se compara contra ESTE valor.
        int expectedCount = fileData.m_Groups.Count();

        string tmpPath = LFPG_TerritoryConfig.GetGroupsTmpPath();
        string bakPath = LFPG_TerritoryConfig.GetGroupsBackupPath();
        string finalPath = LFPG_TerritoryConfig.GetGroupsPath();

        // 1. Serializar a tmp. Si falla, no se toca ni el final ni el bak.
        string saveError = "";
        if (!JsonFileLoader<LFPG_GroupsFileData>.SaveFile(tmpPath, fileData, saveError))
        {
            string err1 = "SaveGroups: SaveFile to tmp failed: ";
            err1 = err1 + saveError;
            LFPG_Log.Error(err1);
            return;
        }

        // 2. Releer el tmp: un fichero que no deserializa no se promueve.
        LFPG_GroupsFileData verifyData = new LFPG_GroupsFileData();
        string verifyError = "";
        if (!JsonFileLoader<LFPG_GroupsFileData>.LoadFile(tmpPath, verifyData, verifyError))
        {
            string err2 = "SaveGroups: verify read of tmp failed: ";
            err2 = err2 + verifyError;
            LFPG_Log.Error(err2);
            return;
        }
        if (!verifyData.m_Groups || verifyData.m_Groups.Count() != expectedCount)
        {
            LFPG_Log.Error("SaveGroups: tmp group count mismatch. Rotation aborted.");
            return;
        }

        // 3. Backup del actual. Cualquier fallo aborta antes de tocar el final.
        if (FileExist(finalPath))
        {
            if (FileExist(bakPath))
            {
                if (!DeleteFile(bakPath))
                {
                    LFPG_Log.Error("SaveGroups: cannot delete stale backup. Rotation aborted.");
                    return;
                }
            }
            if (!CopyFile(finalPath, bakPath))
            {
                LFPG_Log.Error("SaveGroups: cannot copy final to backup. Rotation aborted.");
                return;
            }
        }

        // 4. Promover tmp -> final SIN borrar el final antes. Solo si la copia
        //    directa falla se borra y se reintenta una vez.
        if (!CopyFile(tmpPath, finalPath))
        {
            if (FileExist(finalPath))
            {
                if (!DeleteFile(finalPath))
                {
                    LFPG_Log.Error("SaveGroups: cannot delete final before retry. Rotation aborted.");
                    return;
                }
            }
            if (!CopyFile(tmpPath, finalPath))
            {
                LFPG_Log.Error("SaveGroups: cannot promote tmp to final. Backup still holds prior state.");
                return;
            }
        }

        // 5. Limpiar tmp solo tras una rotacion completa.
        if (FileExist(tmpPath))
        {
            DeleteFile(tmpPath);
        }
    }

    void LoadGroups()
    {
        string filePath = LFPG_TerritoryConfig.GetGroupsPath();
        string bakPath = LFPG_TerritoryConfig.GetGroupsBackupPath();
        string dirPath = LFPG_TerritoryConfig.GetConfigDir();

        // Crear directorio si no existe
        if (!FileExist(dirPath))
        {
            MakeDirectory(dirPath);
        }

        LFPG_GroupsFileData fileData = new LFPG_GroupsFileData();
        string loadError = "";
        bool loaded = false;

        // Carga primaria.
        if (FileExist(filePath))
        {
            if (JsonFileLoader<LFPG_GroupsFileData>.LoadFile(filePath, fileData, loadError))
            {
                // Un fichero valido con 0 grupos es un estado legitimo (se disolvio
                // el ultimo grupo). Tratarlo como corrupto hacia caer al backup y
                // RESUCITAR grupos ya borrados.
                if (fileData && fileData.m_Groups)
                    loaded = true;
            }
        }

        // FIX PERS-1: el backup se intenta TAMBIEN cuando el primario no existe.
        // Ese es justo el estado que deja un crash durante la rotacion de SaveGroups.
        if (!loaded)
        {
            if (FileExist(bakPath))
            {
                fileData = new LFPG_GroupsFileData();
                if (JsonFileLoader<LFPG_GroupsFileData>.LoadFile(bakPath, fileData, loadError))
                {
                    if (fileData && fileData.m_Groups && fileData.m_Groups.Count() > 0)
                    {
                        loaded = true;
                        if (!FileExist(filePath))
                        {
                            CopyFile(bakPath, filePath);
                            LFPG_Log.Info("groups.json missing; recovered from backup and promoted.");
                        }
                        else
                        {
                            LFPG_Log.Info("Primary groups.json unusable; loaded from backup.");
                        }
                    }
                }
            }
        }

        if (!loaded)
        {
            string freshMsg = "LoadGroups: no usable data in primary or backup. Starting fresh. Last error: ";
            freshMsg = freshMsg + loadError;
            LFPG_Log.Error(freshMsg);
            return;
        }

        // No tragarse un formato mas nuevo que el que este build entiende.
        if (fileData.m_Version > LFPG_GROUPS_FILE_VERSION)
        {
            string verErr = "LoadGroups: file version ";
            verErr = verErr + fileData.m_Version.ToString();
            verErr = verErr + " is newer than supported ";
            verErr = verErr + LFPG_GROUPS_FILE_VERSION.ToString();
            verErr = verErr + ". Refusing to load.";
            LFPG_Log.Error(verErr);
            return;
        }

        // Reconstruir estructuras en memoria con VALIDACION por grupo (FIX I-22)
        int i;
        int count = fileData.m_Groups.Count();
        int skippedCount = 0;
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_GroupData group = fileData.m_Groups[i];

            // Validacion de integridad: descarta grupos corruptos silenciosamente
            if (!group || group.m_GroupID == "")
            {
                skippedCount = skippedCount + 1;
                continue;
            }
            if (group.m_LeaderUID == "")
            {
                string skipLeader = "Skipping corrupt group (no leader): ";
                skipLeader = skipLeader + group.m_GroupID;
                LFPG_Log.Error(skipLeader);
                skippedCount = skippedCount + 1;
                continue;
            }
            if (!group.m_Members || group.m_Members.Count() == 0)
            {
                string skipMembers = "Skipping corrupt group (no members): ";
                skipMembers = skipMembers + group.m_GroupID;
                LFPG_Log.Error(skipMembers);
                skippedCount = skippedCount + 1;
                continue;
            }

            // Sanear nombre si es temporal o invalido (se renombrara al siguiente login)
            if (group.m_GroupName == "")
            {
                group.m_GroupName = LFPG_GroupData.GenerateTempName(group.m_LeaderUID);
            }

            // Sanear counters negativos
            if (group.m_DeployedCount < 0)
                group.m_DeployedCount = 0;
            if (group.m_GardenPlotCount < 0)
                group.m_GardenPlotCount = 0;

            m_Groups.Set(group.m_GroupID, group);

            // Reconstruir nombre set (excluyendo prefijo temporal #TEMP# - FIX C-5)
            string tempPrefixCheck = "#TEMP#";
            int tpLen = tempPrefixCheck.Length();
            bool isTempName = false;
            if (group.m_GroupName.Length() >= tpLen)
            {
                string namePrefix = group.m_GroupName.Substring(0, tpLen);
                if (namePrefix == tempPrefixCheck)
                    isTempName = true;
            }
            if (!isTempName && group.m_GroupName != "")
            {
                m_GroupNames.Set(group.m_GroupName, true);
            }

            // Reconstruir player->group map con validacion de UID
            int j;
            int memberCount = group.m_Members.Count();
            for (j = 0; j < memberCount; j = j + 1)
            {
                LFPG_MemberData member = group.m_Members[j];
                if (member && member.m_PlayerUID != "")
                {
                    m_PlayerToGroup.Set(member.m_PlayerUID, group.m_GroupID);
                }
            }
        }

        if (skippedCount > 0)
        {
            string skipMsg = "LoadGroups: skipped ";
            skipMsg = skipMsg + skippedCount.ToString();
            skipMsg = skipMsg + " corrupt group entries.";
            LFPG_Log.Error(skipMsg);
        }

        string logMsg = "Loaded ";
        logMsg = logMsg + m_Groups.Count().ToString();
        logMsg = logMsg + " groups from JSON.";
        LFPG_Log.Info(logMsg);
    }
};
