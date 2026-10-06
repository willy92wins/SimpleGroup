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
//  - Recoverable JSON save (tmp -> bak -> final), not atomic
//  - RPC rate limiting por jugador
//  - Deploy/Garden counters O(1) (no proximity scan en runtime)
// ============================================================================

// Logical payload version. Disk envelope versions belong to LFPG_GroupsStorage.
const int LFPG_GROUPS_FILE_VERSION = 1;

class LFPG_GroupManager
{
    // Radius scans per validation tick. The interval still gates each flag.
    static const int LFPG_BASE_REFRESH_BATCH = 4;
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

    // PERF (issue #24, PR1): tier durations resolved once in Init so
    // ComputeCurrentRaiseProgress (called per territory/build check) skips the
    // config lookup + array-bounds walk. Falls back to config when empty.
    protected ref array<int> m_TierDurCache;

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

    // Lifetime upkeep of protected vehicles between validation ticks (1 s).
    // Created only with the vehicle option on.
    protected ref Timer m_VehicleLifetimeTimer;

    // PERF (issue #24, PR2): unico timer de drenaje de baterias T3.
    // Antes cada T3 tenia el suyo (m_BatteryDrainTimer): N timers, N lookups
    // de config por tick. El drain por bandera y periodo no cambian.
    // El timer global solo despacha (cada 1 s); cada bateria conserva su propio
    // vencimiento a 10 s desde que se registro, como hacia Timer.Run al insertar.
    static const float LFPG_BATTERY_SCHED_SEC = 1.0;
    static const int LFPG_BATTERY_DRAIN_PERIOD_MS = 10000;
    protected ref Timer m_BatteryTickTimer;

    // T3 con bateria drenando, independiente de grupos: incluye banderas
    // abandonadas, pendientes y las de grupos disueltos. Refs debiles: una
    // entidad borrada queda null y el tick la poda. Arrays paralelos.
    protected ref array<LFPG_Flag_T3> m_BatteryFlags;
    protected ref array<int> m_BatteryDueMs;

    // FIX H4+H5: Buffers reutilizables (no allocar en ticks)
    protected ref array<string> m_OrphanBuffer;
    protected ref array<Man> m_PlayerSearchBuffer;
    protected ref array<Man> m_SyncRecipientBuffer;
    protected ref array<Object> m_RecalObjectBuffer;
    protected ref array<CargoBase> m_RecalCargoBuffer;

    // Flags restored by the engine after MissionServer.OnInit. LoadGroups runs
    // inside Init, before any flag exists, so a missing group at AfterStoreLoad
    // is not proof the flag is an orphan. The first validation tick resolves them.
    protected ref array<LFPG_FlagBase> m_PendingFlags;

    // AUDIT #10 L1-F05: banderas que calcularon estado con m_Config == null (las
    // entidades se restauran dentro de super.OnInit, antes de Init()). Se
    // re-inicializan en cuanto la config esta cargada.
    protected ref array<LFPG_FlagBase> m_ConfigPendingFlags;

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
    // Only this live session may finish its own verified pending promotion.
    protected bool m_HasVerifiedGroupsTmp;

    // One-shot boot audit after initial restoration. Pending registration also
    // handles entities restored before Init; do not assume one engine order.
    protected bool m_BootAuditDone;

    // groups.json or its backup exists but could not be used. Saves must not
    // replace those files. A fresh install (neither file present) leaves this false.
    protected bool m_GroupsLoadFailed;
    protected bool m_GroupsSourceMissing;
    // Retired C2S routes are ignored. Logged once so a modified client cannot flood the RPT.
    protected bool m_LoggedRetiredRpc;

    // FIX F: Recalibracion periodica de counters
    // Primer tick: siempre recalibrar (post-startup)
    // Ticks siguientes: cada m_RecalibrationIntervalSeconds (configurable, default 30s)
    protected bool m_NeedsCounterRecalibration;
    protected int m_RecalibrationTickCounter;

    // Last base-lifetime refresh time (ms) per group. Allocated once, not per tick.
    protected ref map<string, int> m_BaseRefreshAt;
    protected int m_BaseRefreshCursor;

    // Vehicle protection under raised flags (config v6), its scan cursor and the
    // flag group ids of the current tick (map.GetKey is O(n) per call).
    protected ref LFPG_VehicleProtection m_VehicleProtection;
    protected int m_VehicleScanCursor;
    protected ref array<string> m_VehicleScanIDs;
    // groupID -> the flag had its first vehicle scan this session. A flag gets it on
    // the first update after it registers, outside the batch (after boot: all).
    protected ref map<string, bool> m_VehicleFirstScanDone;
    // Group ids first-scanned in the current update; the batch skips them.
    protected ref array<string> m_VehicleFirstScanned;

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
        m_SyncRecipientBuffer = new array<Man>;
        m_RecalObjectBuffer = new array<Object>;
        m_RecalCargoBuffer = new array<CargoBase>;
        m_PendingFlags = new array<LFPG_FlagBase>;
        m_ConfigPendingFlags = new array<LFPG_FlagBase>;
        m_OrphanStrikes = new map<string, int>;
        m_BatteryFlags = new array<LFPG_Flag_T3>;
        m_BatteryDueMs = new array<int>;
        m_IsShuttingDown = false;
        m_DissolveDisabled = false;
        m_DissolveDisabledLogged = false;
        m_IsDirty = false;
        m_BootAuditDone = false;
        m_GroupsLoadFailed = false;
        m_LoggedRetiredRpc = false;
        m_NeedsCounterRecalibration = true;
        m_RecalibrationTickCounter = 0;
        m_BaseRefreshAt = new map<string, int>;
        m_BaseRefreshCursor = 0;
        m_VehicleProtection = new LFPG_VehicleProtection();
        m_VehicleScanCursor = 0;
        m_VehicleScanIDs = new array<string>;
        m_VehicleFirstScanDone = new map<string, bool>;
        m_VehicleFirstScanned = new array<string>;
    }

    void ~LFPG_GroupManager()
    {
        if (m_ValidationTimer)
        {
            m_ValidationTimer.Stop();
            m_ValidationTimer = null;
        }
        if (m_VehicleLifetimeTimer)
        {
            m_VehicleLifetimeTimer.Stop();
            m_VehicleLifetimeTimer = null;
        }
        if (m_BatteryTickTimer)
        {
            m_BatteryTickTimer.Stop();
            m_BatteryTickTimer = null;
        }
        if (GetGame())
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.RunQueuedBootAudit);
    }

    // ========================================================================
    // INIT - Llamado desde modded MissionServer.OnInit()
    // ========================================================================
    void Init()
    {
        // Validate the winner before touching recovery files. Future formats
        // and an invalid existing final always require administrator recovery.
        if (!PrepareGroupsFiles())
        {
            m_GroupsLoadFailed = true;
            LFPG_Log.Error("Init: groups recovery refused. READ ONLY; candidates retained.");
        }

        // Cargar config
        m_Config = LFPG_TerritoryConfig.Load();
        LFPG_Log.Info(m_Config.DescribeVehicleProtection());

        // PERF (issue #24, PR1): snapshots used by hot paths. Config lists are
        // immutable after this point, so the classname memo stays valid.
        LFPG_KindMemo.Clear();
        m_TierDurCache = new array<int>;
        int cacheTier;
        for (cacheTier = 1; cacheTier <= 3; cacheTier = cacheTier + 1)
        {
            m_TierDurCache.Insert(m_Config.GetTierDuration(cacheTier));
        }

        // Rehacer con la config real lo calculado antes de cargarla (acciones
        // por tier y SetFullyRaised de T3); despues cargar los grupos.
        int cfgPendCount = m_ConfigPendingFlags.Count();
        int cpi;
        for (cpi = 0; cpi < cfgPendCount; cpi = cpi + 1)
        {
            LFPG_FlagBase cfgFlag = m_ConfigPendingFlags[cpi];
            if (cfgFlag)
                cfgFlag.OnServerConfigLoaded();
        }
        m_ConfigPendingFlags.Clear();
        string cfgPendMsg = "Init: flags re-initialized after config load: ";
        cfgPendMsg = cfgPendMsg + cfgPendCount.ToString();
        LFPG_Log.Info(cfgPendMsg);

        // Keep the boot gate; pending flags cover either restoration order.
        LoadGroups();

        // Resolve flags already restored, without abandoning any pending ID.
        ResolvePendingFlags();

        // With the option off, vehicles.json is neither read nor written.
        if (m_Config.m_OverrideVehicleLifetime)
        {
            m_VehicleProtection.Load();
            m_VehicleLifetimeTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
            m_VehicleLifetimeTimer.Run(1.0, this, "OnVehicleLifetimeTick", null, true);
        }

        // One second, once. The validation tick repeats the audit only if this
        // call has not run yet. Not a repeating CallLater (the 4.5 h timer bug).
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.RunQueuedBootAudit, 1000, false);

        // Timer periodico de validacion (FIX M-2: interval configurable)
        // Usa Timer class (NO CallLater) - inmune al bug de 4.5h
        float tickSec = 60.0;
        if (m_Config && m_Config.m_ValidationTickSeconds > 0)
            tickSec = m_Config.m_ValidationTickSeconds;
        m_ValidationTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        m_ValidationTimer.Run(tickSec, this, "OnValidationTick", null, true);

        // PERF (issue #24, PR2): drenaje de baterias T3, 1 timer global en vez
        // de 1 por bandera. Usa Timer class, igual que validacion.
        m_BatteryTickTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        m_BatteryTickTimer.Run(LFPG_BATTERY_SCHED_SEC, this, "OnBatteryTick", null, true);

        string msg = "GroupManager initialized. Groups: ";
        msg = msg + m_Groups.Count().ToString();
        LFPG_Log.Info(msg);
    }

    LFPG_TerritoryConfig GetConfig()
    {
        return m_Config;
    }

    // PERF (issue #24, PR1): cached tier duration for raise-progress math.
    int GetCachedTierDuration(int tier)
    {
        int idx = tier - 1;
        if (idx < 0)
            idx = 0;
        if (m_TierDurCache && idx < m_TierDurCache.Count())
            return m_TierDurCache[idx];
        if (m_Config)
            return m_Config.GetTierDuration(tier);
        return 172800;
    }

    // ========================================================================
    // PENDING FLAGS - banderas que cargaron antes que los grupos
    // ========================================================================
    // Llamado por LFPG_FlagBase cuando calcula estado sin config (boot).
    void QueueFlagConfigReinit(LFPG_FlagBase flag)
    {
        if (!flag)
            return;
        if (m_ConfigPendingFlags.Find(flag) >= 0)
            return;
        m_ConfigPendingFlags.Insert(flag);
    }

    void RegisterPendingFlag(LFPG_FlagBase flag)
    {
        if (!flag)
            return;

        // A restored owner ID proves this is not a fresh world. Never clear it
        // just because the profile directory was moved or lost.
        if (m_GroupsSourceMissing && flag.GetGroupID() != "")
        {
            if (!m_GroupsLoadFailed)
                LFPG_Log.Error("Groups profile missing but owned flags exist. Groups are READ ONLY; restore the profile and restart.");
            m_GroupsLoadFailed = true;
            m_DissolveDisabled = true;
        }

        int count = m_PendingFlags.Count();
        int i;
        for (i = 0; i < count; i = i + 1)
        {
            if (m_PendingFlags[i] == flag)
            {
                if (m_GroupsLoadFailed || m_DissolveDisabled)
                    flag.ApplyFailedLoadLifetime();
                return;
            }
        }

        m_PendingFlags.Insert(flag);
        string pendMsg = "Flag pending, group not loaded yet: ";
        pendMsg = pendMsg + flag.GetGroupID();
        LFPG_Log.Info(pendMsg);

        // Load failed: block the zone now. The audit is still a second away.
        // The long lifetime is preservation only. The flag stays pending.
        if (m_GroupsLoadFailed || m_DissolveDisabled)
        {
            flag.ApplyFailedLoadLifetime();
            float pendProgress = flag.ComputeCurrentRaiseProgress();
            UpdateFlagPositionCache(flag.GetGroupID(), flag.GetPosition(), pendProgress, flag.GetTier());
        }
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

    // Boot audit. Runs once, on the first validation tick, after ResolvePendingFlags.
    // Pending flags cover restoration both before and after Init.
    //
    // Two jobs:
    //  1. Re-bind flags that lost their group id on an earlier boot.
    //     m_FlagPosition is the anchor that survives that, and the flag is in the
    //     world (not in m_PendingFlags: it took the abandoned branch).
    //  2. If too many groups still have no flag, the loaded state is wrong:
    //     automatic dissolve is disabled before it deletes anything.
    //     An unreadable groups file arms the same net even when zero groups loaded.
    void AuditOrphansAtBoot()
    {
        int total = m_Groups.Count();
        if (total == 0)
        {
            if (m_GroupsLoadFailed)
            {
                m_DissolveDisabled = true;
                string emptyNet = "BOOT SAFETY NET: groups file could not be loaded (0 groups). Automatic dissolve DISABLED this session.";
                LFPG_Log.Error(emptyNet);
            }
            return;
        }

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
                if (candidate.GetGroupID() != "" && GroupExists(candidate.GetGroupID()))
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

    // False until the one-shot boot audit has finished. Claim stays closed until then.
    bool IsBootAuditDone()
    {
        return m_BootAuditDone;
    }

    bool CanMutateGroups()
    {
        return m_BootAuditDone && !m_GroupsLoadFailed && !m_IsShuttingDown;
    }

    void SendGroupsUnavailable(PlayerBase player)
    {
        if (player && player.GetIdentity())
            SendErrorToPlayer(player.GetIdentity(), player, "#STR_LFPG_ERR_GROUPS_UNAVAILABLE");
    }

    // Resolve, re-bind, then abandon whatever is still pending. A failed load
    // keeps those flags pending for the session so lifetime and live progress
    // still see them. Runs from the gameplay queue about a second after Init,
    // or from the validation tick if that tick arrives first. Exactly once.
    void RunBootAudit()
    {
        if (m_BootAuditDone)
            return;
        if (m_IsShuttingDown)
            return;

        ResolvePendingFlags();
        AuditOrphansAtBoot();
        ResolvePendingFlags();

        int pendLeftCount = m_PendingFlags.Count();
        int abandonedDeclared = 0;
        int pi;
        for (pi = 0; pi < pendLeftCount; pi = pi + 1)
        {
            LFPG_FlagBase pFlag = m_PendingFlags[pi];
            if (!pFlag)
                continue;

            if (m_GroupsLoadFailed || m_DissolveDisabled)
            {
                string keptID = pFlag.GetGroupID();
                if (IsOwnedRegisteredFlag(pFlag))
                    continue;
                float keptProgress = pFlag.ComputeCurrentRaiseProgress();
                UpdateFlagPositionCache(keptID, pFlag.GetPosition(), keptProgress, pFlag.GetTier());
                string keepMsg = "Boot audit: load failed, flag kept group id ";
                keepMsg = keepMsg + keptID;
                keepMsg = keepMsg + " at ";
                keepMsg = keepMsg + pFlag.GetPosition().ToString();
                LFPG_Log.Error(keepMsg);
                pFlag.ApplyFailedLoadLifetime();
            }
            else
            {
                pFlag.SetGroupID("");
                RegisterAbandonedFlag(pFlag);
                abandonedDeclared = abandonedDeclared + 1;
            }
        }
        // Normal sessions drop the list after abandon. A failed load leaves the
        // flags here: clearing them would freeze progress and stop the top-up.
        if (!m_GroupsLoadFailed && !m_DissolveDisabled)
            m_PendingFlags.Clear();

        string pendLeft = "Boot audit: flags declared abandoned: ";
        pendLeft = pendLeft + abandonedDeclared.ToString();
        LFPG_Log.Info(pendLeft);
        m_BootAuditDone = true;
    }

    // The queued boot audit, about a second after Init. The vehicles restored
    // with the world are bound and protected right after it, not a validation
    // tick later: until their first update, nothing gives back a lifetime that
    // the engine resets. A tick that ran the audit first updates them itself.
    void RunQueuedBootAudit()
    {
        bool auditWasDone = m_BootAuditDone;
        RunBootAudit();
        if (!auditWasDone && m_BootAuditDone)
            UpdateVehicleProtection();
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

        // The queued audit normally runs first. This covers a tick that arrives sooner.
        RunBootAudit();

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
            // An unreadable groups file leaves flags under their own group id so the
            // zone still blocks. Those ids are absent from m_Groups; do not drop them.
            bool removeEntry = false;
            if (!posCache)
            {
                removeEntry = true;
            }
            else if (!m_GroupsLoadFailed && !m_DissolveDisabled)
            {
                if (!m_Groups.Contains(posCache.m_GroupID) && posCache.m_GroupID != LFPG_ABANDONED_GROUP)
                    removeEntry = true;
            }
            if (removeEntry)
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

        MaintainRegisteredFlagLifetimes();
        RefreshRaisedBases();
        UpdateVehicleProtection();
    }

    // Vehicle protection (config v6). Each registered flag gets its first scan of
    // the session on the first update after it registers, outside the batch, so
    // its queued vehicles are bound and topped up at once (after boot: every flag).
    // Then up to LFPG_VehicleProtection.SCAN_BATCH flags are scanned, raised or
    // not, and the first vehicles of every raised flag are protected.
    // Off when the groups could not be loaded (R12). While the boot safety net is
    // armed the world may be the wrong one: protection, icons and real exits go on,
    // but missing vehicles are not counted, queues of missing groups are kept and
    // vehicles.json is not written.
    protected void UpdateVehicleProtection()
    {
        if (!m_Config || !m_Config.m_OverrideVehicleLifetime)
            return;
        if (!m_VehicleProtection || !m_VehicleProtection.IsLoaded())
            return;
        if (m_GroupsLoadFailed)
        {
            m_VehicleProtection.UnprotectAll();
            return;
        }
        if (!m_BootAuditDone)
            return;

        // Misses need a world that can be trusted, and are never counted in a
        // flag's first scan (its vehicles may not be bound yet).
        bool worldTrusted = !m_DissolveDisabled;

        m_VehicleScanIDs.Clear();
        foreach (string flagGroupID, LFPG_FlagBase flagEntry : m_GroupFlags)
        {
            m_VehicleScanIDs.Insert(flagGroupID);
        }
        int scanFlagCount = m_VehicleScanIDs.Count();

        m_VehicleFirstScanned.Clear();
        int fsi;
        for (fsi = 0; fsi < scanFlagCount; fsi = fsi + 1)
        {
            string firstID = m_VehicleScanIDs[fsi];
            if (m_VehicleFirstScanDone.Contains(firstID))
                continue;
            LFPG_FlagBase firstFlag = m_GroupFlags.Get(firstID);
            if (!firstFlag || !IsOwnedRegisteredFlag(firstFlag))
                continue;
            m_VehicleProtection.ScanFlag(firstFlag, firstID, this, m_Config, false);
            m_VehicleFirstScanDone.Set(firstID, true);
            m_VehicleFirstScanned.Insert(firstID);
        }
        if (m_VehicleFirstScanned.Count() > 0)
        {
            string firstMsg = "Vehicle protection first scans: flags=";
            firstMsg = firstMsg + m_VehicleFirstScanned.Count().ToString();
            LFPG_Log.Info(firstMsg);
        }

        if (scanFlagCount > 0)
        {
            if (m_VehicleScanCursor >= scanFlagCount)
                m_VehicleScanCursor = 0;
            int flagsScanned = 0;
            int flagsSeen = 0;
            while (flagsSeen < scanFlagCount && flagsScanned < LFPG_VehicleProtection.SCAN_BATCH)
            {
                string scanID = m_VehicleScanIDs[m_VehicleScanCursor];
                m_VehicleScanCursor = m_VehicleScanCursor + 1;
                if (m_VehicleScanCursor >= scanFlagCount)
                    m_VehicleScanCursor = 0;
                flagsSeen = flagsSeen + 1;

                if (m_VehicleFirstScanned.Find(scanID) >= 0)
                    continue;
                LFPG_FlagBase scanFlag = m_GroupFlags.Get(scanID);
                if (!scanFlag)
                    continue;
                if (!IsOwnedRegisteredFlag(scanFlag))
                    continue;
                m_VehicleProtection.ScanFlag(scanFlag, scanID, this, m_Config, worldTrusted);
                flagsScanned = flagsScanned + 1;
            }
        }

        if (worldTrusted)
            m_VehicleProtection.PruneMissingGroups(this);
        m_VehicleProtection.ApplyProtection(this, m_Config);
        if (worldTrusted)
            m_VehicleProtection.SaveIfDirty();
    }

    // Every second: gives protected vehicles back the lifetime that the engine
    // reset when an item entered them. Same gates as UpdateVehicleProtection; it
    // also runs while the boot safety net is armed, like the protection itself.
    void OnVehicleLifetimeTick()
    {
        if (!m_Config || !m_Config.m_OverrideVehicleLifetime)
            return;
        if (!m_VehicleProtection || !m_VehicleProtection.IsLoaded())
            return;
        if (m_GroupsLoadFailed || !m_BootAuditDone)
            return;
        m_VehicleProtection.KeepProtectedLifetimes(m_Config);
    }

    // Shutdown save of the vehicle queues, behind the same gates as the tick.
    bool SaveVehicleQueuesIfDirty()
    {
        if (!m_VehicleProtection)
            return true;
        if (!m_Config || !m_Config.m_OverrideVehicleLifetime)
            return true;
        if (m_GroupsLoadFailed || m_DissolveDisabled || !m_BootAuditDone)
            return false;
        return m_VehicleProtection.SaveIfDirty();
    }

    // ========================================================================
    // T3 BATTERY DRAIN REGISTRY
    // ========================================================================
    // Llamado por LFPG_Flag_T3 donde antes arrancaba su Timer (insercion con
    // carga, AfterStoreLoad). Re-registrar reinicia el periodo, igual que Timer.Run.
    void RegisterBatteryDrain(LFPG_Flag_T3 flag)
    {
        if (!flag)
            return;

        int dueMs = GetGame().GetTime() + LFPG_BATTERY_DRAIN_PERIOD_MS;
        int idx = m_BatteryFlags.Find(flag);
        if (idx >= 0)
        {
            m_BatteryDueMs[idx] = dueMs;
            return;
        }
        m_BatteryFlags.Insert(flag);
        m_BatteryDueMs.Insert(dueMs);
    }

    // Llamado por LFPG_Flag_T3 donde antes paraba su Timer (desconexion, borrado).
    void UnregisterBatteryDrain(LFPG_Flag_T3 flag)
    {
        if (!flag)
            return;
        int idx = m_BatteryFlags.Find(flag);
        if (idx < 0)
            return;
        m_BatteryFlags.Remove(idx);
        m_BatteryDueMs.Remove(idx);
    }

    // PERF (issue #24, PR2): tick global de drenaje T3, cada 1 s.
    // Solo drena las baterias cuyo vencimiento llego (cada 10 s por bateria),
    // con el m_BatteryDrainPerSecond leido UNA vez por tick (antes era una
    // lectura de config por bandera y por tick, mas un Timer por entidad).
    // Recorre hacia atras: Remove mueve el ultimo elemento al hueco y ese ya
    // se proceso.
    void OnBatteryTick()
    {
        if (m_IsShuttingDown)
            return;
        if (!m_Config)
            return;

        float drainPerSec = m_Config.m_BatteryDrainPerSecond;
        int nowMs = GetGame().GetTime();

        int bi;
        for (bi = m_BatteryFlags.Count() - 1; bi >= 0; bi = bi - 1)
        {
            LFPG_Flag_T3 t3 = m_BatteryFlags[bi];
            if (!t3)
            {
                m_BatteryFlags.Remove(bi);
                m_BatteryDueMs.Remove(bi);
                continue;
            }

            int dueMs = m_BatteryDueMs[bi];
            if (nowMs - dueMs < 0)
                continue;

            // Periodo fijo como el Timer repetitivo; tras un hitch largo no se
            // encadenan drenajes atrasados.
            dueMs = dueMs + LFPG_BATTERY_DRAIN_PERIOD_MS;
            if (dueMs - nowMs <= 0)
                dueMs = nowMs + LFPG_BATTERY_DRAIN_PERIOD_MS;
            m_BatteryDueMs[bi] = dueMs;

            if (!t3.BatteryTick(drainPerSec))
            {
                m_BatteryFlags.Remove(bi);
                m_BatteryDueMs.Remove(bi);
            }
        }
    }

    // Top up every flag that still belongs to a group. Abandoned flags are not in the map.
    protected void MaintainRegisteredFlagLifetimes()
    {
        int lifeCount = m_GroupFlags.Count();
        int lifeIdx;
        for (lifeIdx = 0; lifeIdx < lifeCount; lifeIdx = lifeIdx + 1)
        {
            LFPG_FlagBase lifeFlag = m_GroupFlags.GetElement(lifeIdx);
            if (!lifeFlag)
                continue;
            if (lifeFlag.GetGroupID() == "")
                continue;
            lifeFlag.ApplyGroupLifetime();
        }

        // Failed load only. Pending flags are not registered and do not refresh a base.
        if (!m_GroupsLoadFailed && !m_DissolveDisabled)
            return;

        int preserveCount = m_PendingFlags.Count();
        int preserveIdx;
        for (preserveIdx = 0; preserveIdx < preserveCount; preserveIdx = preserveIdx + 1)
        {
            LFPG_FlagBase preserveFlag = m_PendingFlags[preserveIdx];
            if (!preserveFlag)
                continue;
            if (preserveFlag.GetGroupID() == "")
                continue;
            preserveFlag.RefreshVisualProgress();
            preserveFlag.ApplyFailedLoadLifetime();
        }
    }

    // At most LFPG_BASE_REFRESH_BATCH radius scans per tick, and each group at most
    // once per m_RecalibrationIntervalSeconds. Lowered flags (progress <= 0) are skipped.
    protected void RefreshRaisedBases()
    {
        if (!m_Config)
            return;
        if (m_Config.m_MinRefreshLifetime < 0)
            return;

        int refreshCount = m_GroupFlags.Count();
        if (refreshCount == 0)
            return;

        int nowMs = GetGame().GetTime();
        int intervalMs = m_Config.m_RecalibrationIntervalSeconds * 1000;
        int scanned = 0;
        int seen = 0;

        if (m_BaseRefreshCursor >= refreshCount)
            m_BaseRefreshCursor = 0;

        while (seen < refreshCount && scanned < LFPG_BASE_REFRESH_BATCH)
        {
            string refreshID = m_GroupFlags.GetKey(m_BaseRefreshCursor);
            m_BaseRefreshCursor = m_BaseRefreshCursor + 1;
            if (m_BaseRefreshCursor >= refreshCount)
                m_BaseRefreshCursor = 0;
            seen = seen + 1;

            LFPG_FlagBase refreshFlag = m_GroupFlags.Get(refreshID);
            if (!refreshFlag)
                continue;
            if (!IsOwnedRegisteredFlag(refreshFlag))
                continue;

            float refreshProgress = refreshFlag.ComputeCurrentRaiseProgress();
            if (refreshProgress <= 0.0)
                continue;

            int lastRefresh = 0;
            bool hasStamp = m_BaseRefreshAt.Find(refreshID, lastRefresh);
            if (hasStamp)
            {
                int sinceRefresh = nowMs - lastRefresh;
                if (sinceRefresh >= 0 && sinceRefresh < intervalMs)
                    continue;
            }

            RefreshBaseAroundFlag(refreshFlag);
            m_BaseRefreshAt.Set(refreshID, nowMs);
            scanned = scanned + 1;
        }
    }

    // Same radius query the furniture recount uses. Resets remaining lifetime to max
    // for objects at or above m_MinRefreshLifetime. Players, creatures and the flag are skipped.
    // With vehicle protection on, vehicles and everything they carry are skipped too:
    // only the protected slots keep a vehicle alive.
    void RefreshBaseAroundFlag(LFPG_FlagBase flag)
    {
        if (!flag || !m_Config)
            return;
        if (m_Config.m_MinRefreshLifetime < 0)
            return;
        if (!IsOwnedRegisteredFlag(flag))
            return;

        bool skipVehicles = m_Config.m_OverrideVehicleLifetime;
        vector refreshPos = flag.GetPosition();
        float refreshRadius = m_Config.m_BuildRadiusMeters;

        m_RecalObjectBuffer.Clear();
        m_RecalCargoBuffer.Clear();
        GetGame().GetObjectsAtPosition(refreshPos, refreshRadius, m_RecalObjectBuffer, m_RecalCargoBuffer);

        float lifeThreshold = m_Config.m_MinRefreshLifetime;
        int refreshObjCount = m_RecalObjectBuffer.Count();
        int refreshObj;
        for (refreshObj = 0; refreshObj < refreshObjCount; refreshObj = refreshObj + 1)
        {
            Object refreshObjRef = m_RecalObjectBuffer[refreshObj];
            if (!refreshObjRef)
                continue;
            if (refreshObjRef == flag)
                continue;

            EntityAI refreshEnt = EntityAI.Cast(refreshObjRef);
            if (!refreshEnt)
                continue;
            if (refreshEnt.IsMan())
                continue;
            if (refreshEnt.IsPlayer())
                continue;
            if (refreshEnt.IsAnimal())
                continue;
            if (refreshEnt.IsZombie())
                continue;
            if (refreshEnt.IsDayZCreature())
                continue;
            if (skipVehicles)
            {
                Transport refreshVehicle = Transport.Cast(refreshEnt.GetHierarchyRoot());
                if (refreshVehicle)
                    continue;
            }

            float lifeMax = refreshEnt.GetLifetimeMax();
            if (lifeMax < lifeThreshold)
                continue;

            refreshEnt.SetLifetime(lifeMax);
        }
    }

    // Called from LFPG_FlagBase.SetFullyRaised when a raise finishes.
    // Abandoned flags (T3 power latch included) are not registered and do not refresh.
    void NotifyFlagRaised(LFPG_FlagBase flag)
    {
        if (!flag || !m_Config)
            return;
        if (m_Config.m_MinRefreshLifetime < 0)
            return;
        if (!IsOwnedRegisteredFlag(flag))
            return;

        RefreshBaseAroundFlag(flag);

        string raisedID = flag.GetGroupID();
        if (raisedID != "")
        {
            m_BaseRefreshAt.Set(raisedID, GetGame().GetTime());
        }
    }

    // Duplicate or leftover flag: drop pending registration and an abandoned
    // position row at this spot. Does not touch the group's registered cache.
    void ReleaseUnregisteredFlag(LFPG_FlagBase flag)
    {
        if (!flag)
            return;

        int pendIdx;
        int pendCount = m_PendingFlags.Count();
        for (pendIdx = 0; pendIdx < pendCount; pendIdx = pendIdx + 1)
        {
            if (m_PendingFlags[pendIdx] == flag)
            {
                m_PendingFlags.Remove(pendIdx);
                break;
            }
        }

        RemoveAbandonedFlagPosition(flag.GetPosition());

        // Pending flags are cached under their persisted ID, not ABANDONED.
        // Never remove a registered flag's row when deleting a duplicate.
        string pendingID = flag.GetGroupID();
        if (pendingID != "" && !GetGroupFlag(pendingID))
        {
            RemoveFlagPositionCache(pendingID);
            // Another pending flag can share this ID. Keep its protection.
            for (pendIdx = 0; pendIdx < m_PendingFlags.Count(); pendIdx = pendIdx + 1)
            {
                LFPG_FlagBase remainingPending = m_PendingFlags[pendIdx];
                if (remainingPending && remainingPending.GetGroupID() == pendingID)
                    UpdateFlagPositionCache(pendingID, remainingPending.GetPosition(), remainingPending.ComputeCurrentRaiseProgress(), remainingPending.GetTier());
            }
        }
    }

    // True only when this entity is the flag registered for a group that still exists.
    bool IsOwnedRegisteredFlag(LFPG_FlagBase flag)
    {
        if (!flag)
            return false;

        string ownedID = flag.GetGroupID();
        if (ownedID == "")
            return false;
        if (!m_Groups.Contains(ownedID))
            return false;

        LFPG_FlagBase ownedFlag = GetGroupFlag(ownedID);
        if (!ownedFlag)
            return false;
        if (ownedFlag != flag)
            return false;

        return true;
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
        flag.ApplyGroupLifetime();

        // FIX C4: Restaurar MemberCount desde datos del grupo (tras restart)
        // + Sincronizar posicion persistida para fallback en LIGHTWEIGHT_SYNC
        if (m_Groups.Contains(groupID))
        {
            LFPG_GroupData group = m_Groups.Get(groupID);
            if (group)
            {
                flag.SetMemberCount(group.GetMemberCount());
                group.m_FlagPosition = flag.GetPosition();
                // World tier wins. An upgrade saved to JSON and then lost in a crash
                // before the CE write would otherwise keep the new tier with the old flag.
                if (group.m_Tier != flag.GetTier())
                {
                    int previousTier = group.m_Tier;
                    group.m_Tier = flag.GetTier();
                    MarkDirty();
                    string tierMsg = "RegisterFlag: group ";
                    tierMsg = tierMsg + groupID;
                    tierMsg = tierMsg + " tier ";
                    tierMsg = tierMsg + previousTier.ToString();
                    tierMsg = tierMsg + " corrected to flag tier ";
                    tierMsg = tierMsg + group.m_Tier.ToString();
                    LFPG_Log.Info(tierMsg);
                }
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
        if (m_BaseRefreshAt.Contains(groupID))
        {
            m_BaseRefreshAt.Remove(groupID);
        }
        // The next flag registered for this group (an upgrade) gets its own first
        // vehicle scan. The vehicle queue itself stays with the group.
        m_VehicleFirstScanDone.Remove(groupID);
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

        // Abandoned flags share no group id, so a group-id match would overwrite
        // the first abandoned entry. Match those by the same 2 m test as a claim.
        if (groupID == LFPG_ABANDONED_GROUP)
        {
            for (i = 0; i < count; i = i + 1)
            {
                LFPG_FlagPositionCache abandonedCache = m_FlagPositions[i];
                if (!abandonedCache)
                    continue;
                if (abandonedCache.m_GroupID != LFPG_ABANDONED_GROUP)
                    continue;

                float adx = pos[0] - abandonedCache.m_Position[0];
                float adz = pos[2] - abandonedCache.m_Position[2];
                float adistSq = (adx * adx) + (adz * adz);
                if (adistSq < 4.0)
                {
                    abandonedCache.Set(pos, groupID, progress, tier);
                    return;
                }
            }

            LFPG_FlagPositionCache abandonedNew = new LFPG_FlagPositionCache();
            abandonedNew.Set(pos, groupID, progress, tier);
            m_FlagPositions.Insert(abandonedNew);
            return;
        }

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
    // live raise progress is above zero. A registered flag wins. With none, a
    // pending flag of that group at the cached position supplies live progress.
    // Otherwise the cached value is kept. Empty string means no foreign owner.
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
            else
            {
                LFPG_FlagBase foPending = FindPendingFlagAt(foEntry.m_GroupID, foEntry.m_Position);
                if (foPending)
                    liveProgress = foPending.ComputeCurrentRaiseProgress();
            }
            if (liveProgress <= 0.0)
                continue;

            nearestDistSq = foDistSq;
            nearestID = foEntry.m_GroupID;
        }
        return nearestID;
    }

    // Pending flag of this group whose position matches the cache entry.
    // Used when the group has no registered flag. Null keeps the cached progress.
    protected LFPG_FlagBase FindPendingFlagAt(string groupID, vector pos)
    {
        if (groupID == "")
            return null;

        int pendFind;
        int pendFindCount = m_PendingFlags.Count();
        for (pendFind = 0; pendFind < pendFindCount; pendFind = pendFind + 1)
        {
            LFPG_FlagBase pendFindFlag = m_PendingFlags[pendFind];
            if (!pendFindFlag)
                continue;
            if (pendFindFlag.GetGroupID() != groupID)
                continue;

            vector pendFindPos = pendFindFlag.GetPosition();
            float pendDx = pos[0] - pendFindPos[0];
            float pendDz = pos[2] - pendFindPos[2];
            float pendDistSq = (pendDx * pendDx) + (pendDz * pendDz);
            if (pendDistSq < 4.0)
                return pendFindFlag;
        }
        return null;
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
            // Keep routine counter updates at debug level.
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

            // AUDIT #10 L2-F08 / P1-F12: mismas reglas que el camino vivo. Las
            // listas A (NoBaseRequired) y B (Unrestricted) no cuentan en ningun
            // handler de placement ni de drop (ModdedItemBase OnPlacementComplete
            // y drop, ModdedBaseBuildingBase, ModdedGardenPlot).
            if (m_Config && (m_Config.IsNoBaseRequired(ent) || m_Config.IsUnrestricted(ent)))
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
        if (!CanMutateGroups())
            return "";

        // Validaciones
        if (m_PlayerToGroup.Contains(playerUID))
            return "";

        if (m_GroupNames.Contains(GroupNameKey(groupName)))
            return "";

        if (!flag)
            return "";

        // Crear ID
        string groupID = LFPG_GroupData.GenerateGroupID(playerUID);
        if (m_Groups.Contains(groupID))
        {
            LFPG_Log.Error("CreateGroup: generated group ID already exists; creation rejected");
            return "";
        }

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
            m_GroupNames.Set(GroupNameKey(groupName), true);
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
        if (m_GroupsLoadFailed)
            return;

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

        // Legacy v1 profiles may contain names that normalize to the same key.
        // Keep the reservation until the last group using that name dissolves.
        if (!IsTempGroupName(group.m_GroupName) && m_GroupNames.Contains(GroupNameKey(group.m_GroupName)))
        {
            string releasedNameKey = GroupNameKey(group.m_GroupName);
            bool nameStillUsed = false;
            foreach (string otherGroupID, LFPG_GroupData otherGroup: m_Groups)
            {
                if (otherGroup && otherGroupID != groupID && !IsTempGroupName(otherGroup.m_GroupName) && GroupNameKey(otherGroup.m_GroupName) == releasedNameKey)
                {
                    nameStillUsed = true;
                    break;
                }
            }
            if (!nameStillUsed)
                m_GroupNames.Remove(releasedNameKey);
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

        // The vehicle queue belongs to the group: icons go off, lifetimes stay.
        if (m_VehicleProtection)
            m_VehicleProtection.DropGroup(groupID, "dissolved");

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
        if (!CanMutateGroups())
            return false;

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
        // (grupo disuelto mientras terminaba la accion de unirse)
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
        if (!CanMutateGroups())
            return false;

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
        group.m_Members.RemoveOrdered(memberIdx);

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
        if (!CanMutateGroups())
            return false;

        if (!m_Groups.Contains(groupID))
            return false;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return false;

        if (!group.IsLeader(currentLeaderUID))
            return false;

        if (newLeaderUID == currentLeaderUID)
            return false;

        if (!group.IsMember(newLeaderUID))
            return false;

        group.m_LeaderUID = newLeaderUID;
        MarkDirty();

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
        if (!CanMutateGroups())
            return false;

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
        if (!CanMutateGroups())
            return false;

        if (!oldFlag || groupID == "")
            return false;

        if (GetGroupFlag(groupID) != oldFlag)
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

        // The vanilla flag (Material_FPole_Flag) moves to the new entity before anything else
        // changes; if it cannot, the new entity is deleted and the old one keeps its flag and
        // materials. Runs before TransferDataFrom, which raises the new flag and locks the T3 slot.
        string bannerSlotName = "Material_FPole_Flag";
        int bannerSlot = InventorySlots.GetSlotIdFromString(bannerSlotName);
        EntityAI banner = oldFlag.GetInventory().FindAttachment(bannerSlot);
        if (banner)
        {
            if (!newFlag.ServerTakeEntityAsAttachmentEx(banner, bannerSlot))
            {
                string bannerErr = "UpgradeFlag: cannot move the flag to ";
                bannerErr = bannerErr + newClassName;
                LFPG_Log.Error(bannerErr);
                newFlag.SetSkipDissolveOnDelete();
                GetGame().ObjectDelete(newFlag);
                return false;
            }
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
    static string GroupNameKey(string name)
    {
        name.TrimInPlace();
        name.ToLower();
        return name;
    }

    int ValidateGroupName(string name)
    {
        name.TrimInPlace();
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
        if (m_GroupNames.Contains(GroupNameKey(name)))
            return LFPG_NAME_TAKEN;

        return LFPG_NAME_OK;
    }

    // ========================================================================
    // RPC RATE LIMITING - Anti-spam
    // FIX M-2: Throttle configurable via m_Config.m_RpcThrottleMs
    // ========================================================================
    // AUDIT #10 L1-F07/L3-F08: cubo por (UID, tipo de RPC). Antes un unico
    // cubo por UID: un REQUEST_GROUP_DATA automatico (panel/hologram) se comia
    // el Kick/Leave/Transfer/SetName del usuario dentro de la misma ventana.
    protected bool IsRPCThrottled(string playerUID, int rpcType)
    {
        int now = GetGame().GetTime();
        int throttleMs = 500;
        if (m_Config && m_Config.m_RpcThrottleMs > 0)
            throttleMs = m_Config.m_RpcThrottleMs;

        string key = playerUID;
        key = key + ":";
        key = key + rpcType.ToString();

        if (m_RPCThrottle.Contains(key))
        {
            int lastTime = m_RPCThrottle.Get(key);
            int diff = now - lastTime;
            if (diff < throttleMs)
                return true;
        }
        m_RPCThrottle.Set(key, now);
        PruneRPCThrottle(now, throttleMs);
        return false;
    }

    // Poda acotada: con clave por tipo el mapa crece mas rapido; se limpian
    // entradas caducadas solo al superar el umbral (coste amortizado).
    protected void PruneRPCThrottle(int now, int throttleMs)
    {
        if (m_RPCThrottle.Count() <= 512)
            return;

        array<string> stale = new array<string>;
        int count = m_RPCThrottle.Count();
        int i;
        for (i = 0; i < count; i = i + 1)
        {
            int t = m_RPCThrottle.GetElement(i);
            if (now - t >= throttleMs)
                stale.Insert(m_RPCThrottle.GetKey(i));
        }
        int s;
        for (s = 0; s < stale.Count(); s = s + 1)
        {
            m_RPCThrottle.Remove(stale[s]);
        }
    }

    // Respuesta de rechazo solo para RPCs iniciados por el usuario; los
    // automaticos (REQUEST_GROUP_DATA) siguen descartandose en silencio.
    protected void NotifyRPCThrottled(PlayerIdentity sender, int rpcType)
    {
        bool userRpc = false;
        if (rpcType == LFPG_RPC_C2S_REQUEST_LEAVE)
            userRpc = true;
        else if (rpcType == LFPG_RPC_C2S_REQUEST_KICK)
            userRpc = true;
        else if (rpcType == LFPG_RPC_C2S_REQUEST_TRANSFER)
            userRpc = true;
        else if (rpcType == LFPG_RPC_C2S_SET_GROUP_NAME)
            userRpc = true;
        else if (rpcType == LFPG_RPC_C2S_SET_PANEL_NAME)
            userRpc = true;

        if (!userRpc)
            return;

        PlayerBase pb = PlayerBase.Cast(sender.GetPlayer());
        SendErrorToPlayer(sender, pb, "#STR_LFPG_ERR_RPC_THROTTLED");
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
    // CREATE_GROUP, REQUEST_JOIN, START_INVITE and DESTROY_FLAG are unused.
    // Enum values stay so older clients do not shift the id space.
    protected bool IsRetiredClientRpc(int rpc_type)
    {
        if (rpc_type == LFPG_RPC_C2S_CREATE_GROUP)
            return true;
        if (rpc_type == LFPG_RPC_C2S_REQUEST_JOIN)
            return true;
        if (rpc_type == LFPG_RPC_C2S_START_INVITE)
            return true;
        if (rpc_type == LFPG_RPC_C2S_DESTROY_FLAG)
            return true;
        return false;
    }

    // RPC HANDLER - Server-side dispatcher
    // ========================================================================
    void HandleRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        if (!sender)
            return;

        string senderUID = sender.GetPlainId();
        string senderName = sender.GetName();

        // Rate limiting (por tipo de RPC; rechazo visible para RPCs de usuario)
        if (IsRPCThrottled(senderUID, rpc_type))
        {
            NotifyRPCThrottled(sender, rpc_type);
            return;
        }

        if (IsRetiredClientRpc(rpc_type))
        {
            if (!m_LoggedRetiredRpc)
            {
                m_LoggedRetiredRpc = true;
                LFPG_Log.Debug("Ignored a retired client RPC");
            }
            return;
        }

        if (!CanMutateGroups() && rpc_type != LFPG_RPC_C2S_REQUEST_GROUP_DATA)
        {
            SendGroupsUnavailable(PlayerBase.Cast(sender.GetPlayer()));
            return;
        }

        if (rpc_type == LFPG_RPC_C2S_SET_GROUP_NAME)
        {
            HandleSetGroupName(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_SET_PANEL_NAME)
        {
            HandleSetGroupName(sender, ctx, null, true);
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
        else if (rpc_type == LFPG_RPC_C2S_REQUEST_GROUP_DATA)
        {
            HandleRequestGroupData(sender, ctx, flag);
        }
    }

    // ========================================================================
    // RPC HANDLERS - Individual operations
    // ========================================================================

    protected void HandleSetGroupName(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag, bool fromPanel = false)
    {
        string senderUID = sender.GetPlainId();

        string newName = "";
        if (!ctx.Read(newName))
            return;
        newName.TrimInPlace();

        string requestedGroupID = "";
        if (fromPanel)
        {
            // Nonempty trailing identity also rejects truncated native payloads.
            if (!ctx.Read(requestedGroupID))
                return;
            if (requestedGroupID == "")
                return;
        }

        string groupID = GetPlayerGroupID(senderUID);
        if (groupID == "")
            return;
        if (fromPanel && requestedGroupID != groupID)
            return;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group)
            return;

        // Solo el lider puede renombrar
        if (!group.IsLeader(senderUID))
            return;

        // A real name is final. The dialog is only offered while the name is still #TEMP#.
        if (!IsTempGroupName(group.m_GroupName))
            return;

        // Validar nombre
        int result = ValidateGroupName(newName);
        if (result != LFPG_NAME_OK)
        {
            SendNameResult(sender, flag, result);
            return;
        }

        // Quitar nombre viejo del set (si no era temporal — los temp nunca entraron)
        if (!IsTempGroupName(group.m_GroupName) && m_GroupNames.Contains(GroupNameKey(group.m_GroupName)))
        {
            m_GroupNames.Remove(GroupNameKey(group.m_GroupName));
        }

        // Asignar nuevo nombre (el nombre validado no puede ser temp — LFPG_NAME_ALLOWED_CHARS no incluye '#')
        group.m_GroupName = newName;
        m_GroupNames.Set(GroupNameKey(newName), true);

        MarkDirty();

        SendNameResult(sender, flag, LFPG_NAME_OK);
        // Notificar a todos con sync completo (nombre cambio)
        SendGroupSyncUpdateToMembers(group, LFPG_SYNC_COUNT_CHANGED);
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

    protected void HandleRequestGroupData(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string senderUID = sender.GetPlainId();
        SendPlacementRules(GetPlayerByUID(senderUID));
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
        // AUDIT #10 L1-F01: PlayerBase primero (llega a cualquier distancia);
        // la flag solo como fallback si el PlayerBase no se resuelve.
        PlayerBase senderPlayer = GetPlayerByUID(senderUID);
        Object sendVia = senderPlayer;
        if (!sendVia)
        {
            sendVia = flag;
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
        SendPlacementRules(player);
        if (m_GroupsLoadFailed)
            SendGroupsUnavailable(player);
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

    // Independent policy sync also reaches players without a group. Existing
    // group payloads and saved files keep their original wire/storage format.
    void SendPlacementRules(PlayerBase player)
    {
        if (!m_Config || !player || !player.GetIdentity())
            return;
        ScriptRPC rules = new ScriptRPC();
        int count = 0;
        if (m_Config.m_FurnitureExcludedTypes)
            count = m_Config.m_FurnitureExcludedTypes.Count();
        rules.Write(count);
        for (int i = 0; i < count; i = i + 1)
            rules.Write(m_Config.m_FurnitureExcludedTypes[i]);
        int noBaseCount = 0;
        if (m_Config.m_NoBaseRequiredTypes)
            noBaseCount = m_Config.m_NoBaseRequiredTypes.Count();
        rules.Write(noBaseCount);
        for (int n = 0; n < noBaseCount; n = n + 1)
            rules.Write(m_Config.m_NoBaseRequiredTypes[n]);
        int unrestrictedCount = 0;
        if (m_Config.m_UnrestrictedTypes)
            unrestrictedCount = m_Config.m_UnrestrictedTypes.Count();
        rules.Write(unrestrictedCount);
        for (int u = 0; u < unrestrictedCount; u = u + 1)
            rules.Write(m_Config.m_UnrestrictedTypes[u]);
        rules.Write(LFPG_PLACEMENT_RULES_END);
        rules.Send(player, LFPG_RPC_S2C_PLACEMENT_RULES, true, player.GetIdentity());
    }

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

        // SendGroupSyncFull uses m_PlayerSearchBuffer for online status. Keep
        // recipients separate so its nested call cannot invalidate this loop.
        m_SyncRecipientBuffer.Clear();
        GetGame().GetPlayers(m_SyncRecipientBuffer);

        int memberCount = group.m_Members.Count();
        int playerCount = m_SyncRecipientBuffer.Count();
        int i;
        int j;

        for (i = 0; i < memberCount; i = i + 1)
        {
            LFPG_MemberData member = group.m_Members[i];
            if (!member)
                continue;

            for (j = 0; j < playerCount; j = j + 1)
            {
                Man man = m_SyncRecipientBuffer[j];
                if (!man)
                    continue;
                PlayerIdentity identity = man.GetIdentity();
                if (!identity)
                    continue;
                if (identity.GetPlainId() == member.m_PlayerUID)
                {
                    // AUDIT #10 L1-F01: siempre via PlayerBase (man). La flag
                    // solo llega a clientes dentro de su network bubble; un
                    // miembro lejos perdia altas/bajas/lider/tier.
                    Object syncTarget = man;
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
                    // AUDIT #10 L1-F01: siempre via PlayerBase (ver arriba)
                    Object syncTarget = man;
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
        if (!target)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(result);
        if (flag)
        {
            rpc.Send(flag, LFPG_RPC_S2C_NAME_RESULT, true, target);
            return;
        }

        PlayerBase player = GetPlayerByUID(target.GetPlainId());
        string groupID = GetPlayerGroupID(target.GetPlainId());
        if (!player || groupID == "")
            return;
        rpc.Write(groupID);
        rpc.Send(player, LFPG_RPC_S2C_PANEL_NAME_RESULT, true, target);
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
    // PERSISTENCE - recoverable JSON replacement (not atomic)
    // ========================================================================
    // AUDIT #10 L1-F06 / L3-F03: lectura verificada de un fichero de grupos.
    // true solo si deserializa y su version es soportada; groupCount = grupos.
    // Un fichero valido con 0 grupos es legitimo (se disolvio el ultimo).
    // Validate the entire staged file before installing a single group/index.
    protected bool ValidateGroupsData(LFPG_GroupsFileData data)
    {
        if (!data || !data.m_Groups || data.m_Version != LFPG_GROUPS_FILE_VERSION)
            return false;

        map<string, bool> groupIDs = new map<string, bool>;
        map<string, bool> memberUIDs = new map<string, bool>;
        int i;
        int j;
        for (i = 0; i < data.m_Groups.Count(); i = i + 1)
        {
            LFPG_GroupData record = data.m_Groups[i];
            if (!record || record.m_GroupID == "" || record.m_LeaderUID == "")
                return false;
            if (!record.m_Members || record.m_Members.Count() == 0)
                return false;
            if (groupIDs.Contains(record.m_GroupID))
                return false;
            groupIDs.Set(record.m_GroupID, true);
            bool leaderPresent = false;
            for (j = 0; j < record.m_Members.Count(); j = j + 1)
            {
                LFPG_MemberData recordMember = record.m_Members[j];
                if (!recordMember || recordMember.m_PlayerUID == "")
                    return false;
                if (memberUIDs.Contains(recordMember.m_PlayerUID))
                    return false;
                memberUIDs.Set(recordMember.m_PlayerUID, true);
                if (recordMember.m_PlayerUID == record.m_LeaderUID)
                    leaderPresent = true;
            }
            if (!leaderPresent)
                return false;
        }
        return true;
    }

    protected bool ReadGroupsFile(string path, out int groupCount)
    {
        groupCount = 0;
        LFPG_GroupsFileData probe = new LFPG_GroupsFileData();
        string probeErr = "";
        if (!LFPG_GroupsStorage.LoadFile(path, probe, probeErr))
            return false;
        if (!ValidateGroupsData(probe))
            return false;
        groupCount = probe.m_Groups.Count();
        return true;
    }

    protected bool IsFutureGroupsFile(string path)
    {
        return LFPG_GroupsStorage.ReadVersion(path) > LFPG_GroupsStorage.FILE_VERSION;
    }

    protected bool PrepareGroupsFiles()
    {
        string finalPath = LFPG_TerritoryConfig.GetGroupsPath();
        string tmpPath = LFPG_TerritoryConfig.GetGroupsTmpPath();
        string bakPath = LFPG_TerritoryConfig.GetGroupsBackupPath();
        if (IsFutureGroupsFile(finalPath) || IsFutureGroupsFile(tmpPath) || IsFutureGroupsFile(bakPath))
            return false;
        int count;
        if (FileExist(finalPath))
        {
            if (!ReadGroupsFile(finalPath, count))
                return false;
            // Final wins; preserve an interrupted/unused tmp, even if truncated.
            if (FileExist(tmpPath) && !MoveAside(tmpPath, ".discarded"))
                return false;
            if (FileExist(bakPath) && !ReadGroupsFile(bakPath, count))
            {
                if (!MoveAside(bakPath, ".corrupt"))
                    return false;
            }
            return true;
        }
        if (FileExist(tmpPath))
        {
            if (!ReadGroupsFile(tmpPath, count))
            {
                if (!MoveAside(tmpPath, ".discarded"))
                    return false;
            }
            else
            {
                LFPG_GroupsFileData recovered;
                string error;
                if (!LFPG_GroupsStorage.LoadFile(tmpPath, recovered, error))
                    return false;
                if (!CopyFile(tmpPath, finalPath) || !GroupsFileMatches(finalPath, recovered))
                    return false;
                // Failure to remove tmp is recoverable: next boot final still wins.
                DeleteFile(tmpPath);
                return true;
            }
        }
        if (FileExist(bakPath))
            return ReadGroupsFile(bakPath, count);
        return true;
    }

    // Compare the complete persisted payload, including member identities.
    protected bool GroupsFileMatches(string path, LFPG_GroupsFileData expected)
    {
        LFPG_GroupsFileData actual = new LFPG_GroupsFileData();
        string error = "";
        if (!LFPG_GroupsStorage.LoadFile(path, actual, error))
            return false;
        if (!ValidateGroupsData(actual))
            return false;
        string expectedJSON = "";
        string actualJSON = "";
        if (!JsonFileLoader<LFPG_GroupsFileData>.MakeData(expected, expectedJSON, error, false))
            return false;
        if (!JsonFileLoader<LFPG_GroupsFileData>.MakeData(actual, actualJSON, error, false))
            return false;
        return expectedJSON == actualJSON;
    }

    // Aparta un fichero sin destruirlo: copia a path+suffix y solo borra el
    // original si la copia salio bien. Los apartados anteriores se preservan
    // con un sufijo numerico. false = el original sigue en su sitio.
    protected bool MoveAside(string path, string suffix, bool logFailure = true)
    {
        string aside = path + suffix;
        int asideIndex = 0;
        while (FileExist(aside))
        {
            asideIndex = asideIndex + 1;
            aside = path + suffix + "." + asideIndex.ToString();
        }
        if (!CopyFile(path, aside))
        {
            string mvErr = "MoveAside: cannot copy ";
            mvErr = mvErr + path;
            mvErr = mvErr + " to ";
            mvErr = mvErr + aside;
            if (logFailure)
                LFPG_Log.Error(mvErr);
            return false;
        }
        // A source the text reader cannot read (NUL bytes after a power loss)
        // is verified by its byte count instead, so its bytes are preserved and
        // a valid final or backup can still load.
        string sourceText;
        if (LFPG_GroupsStorage.ReadText(path, sourceText))
        {
            if (!LFPG_GroupsStorage.FilesEqual(path, aside))
                return false;
        }
        else
        {
            int sourceBytes = LFPG_GroupsStorage.ReadByteCount(path);
            if (sourceBytes <= 0 || sourceBytes >= LFPG_GroupsStorage.MAX_BYTES)
                return false;
            if (LFPG_GroupsStorage.ReadByteCount(aside) != sourceBytes)
                return false;
        }
        return DeleteFile(path);
    }

    bool SaveGroupsIfDirty()
    {
        if (!m_IsDirty)
            return true;
        return SaveGroups();
    }

    bool SaveGroups()
    {
        // Failed-load sessions never write ANY groups file, including .session.
        if (m_GroupsLoadFailed)
            return false;
        m_IsDirty = true;
        if (!m_BootAuditDone)
            return false;
        LFPG_GroupsFileData fileData = new LFPG_GroupsFileData();
        fileData.m_Version = LFPG_GROUPS_FILE_VERSION;
        fileData.m_Groups = new array<ref LFPG_GroupData>;

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

        if (!ValidateGroupsData(fileData))
        {
            LFPG_Log.Error("SaveGroups: inconsistent in-memory groups; save refused.");
            return false;
        }

        string tmpPath = LFPG_TerritoryConfig.GetGroupsTmpPath();
        string bakPath = LFPG_TerritoryConfig.GetGroupsBackupPath();
        string finalPath = LFPG_TerritoryConfig.GetGroupsPath();

        if (IsFutureGroupsFile(finalPath) || IsFutureGroupsFile(tmpPath) || IsFutureGroupsFile(bakPath))
        {
            LFPG_Log.Error("SaveGroups: newer groups format found. Save paused; dirty state retained.");
            return false;
        }

        int diskCount;
        bool finalInvalid = FileExist(finalPath) && !ReadGroupsFile(finalPath, diskCount);
        if (finalInvalid && !m_HasVerifiedGroupsTmp)
        {
            LFPG_Log.Error("SaveGroups: final unreadable/invalid. Save paused; dirty state retained.");
            return false;
        }
        string legacySource = finalPath;
        if (!FileExist(legacySource))
            legacySource = bakPath;
        // A verified pending write already preserved its legacy source before
        // creating tmp. Neither its own partial final nor a backup it failed
        // to rotate may prevent completing it.
        if (!finalInvalid && !m_HasVerifiedGroupsTmp && !LFPG_GroupsStorage.PreserveLegacy(legacySource, finalPath + ".pre-v2"))
        {
            LFPG_Log.Error("SaveGroups: cannot preserve v1 migration backup. Save refused.");
            return false;
        }

        // Finish a previous verified write before reusing tmp. Otherwise a
        // truncated retry could erase the only copy of the most recent state.
        bool retryPending = false;
        LFPG_GroupsFileData pendingData = new LFPG_GroupsFileData();
        string pendingError = "";
        if (FileExist(tmpPath))
        {
            if (LFPG_GroupsStorage.LoadFile(tmpPath, pendingData, pendingError))
            {
                if (ValidateGroupsData(pendingData) && !GroupsFileMatches(finalPath, pendingData))
                {
                    retryPending = true;
                    fileData = pendingData;
                }
            }
        }

        // 1. Serializar a tmp. Si falla, no se toca ni el final ni el bak.
        string saveError = "";
        if (!retryPending && !LFPG_GroupsStorage.SaveFile(tmpPath, fileData, saveError))
        {
            string err1 = "SaveGroups: SaveFile to tmp failed: ";
            err1 = err1 + saveError;
            LFPG_Log.Error(err1);
            return false;
        }

        // 2. Re-read the complete payload before touching final/backup.
        if (!GroupsFileMatches(tmpPath, fileData))
        {
            LFPG_Log.Error("SaveGroups: tmp payload mismatch. Rotation aborted.");
            return false;
        }
        m_HasVerifiedGroupsTmp = true;

        // 3. Rotate only a verified final. Backup failure must not freeze saves.
        // An invalid final is moved aside without rotating the backup.
        if (FileExist(finalPath))
        {
            LFPG_GroupsFileData previousData = new LFPG_GroupsFileData();
            string previousError = "";
            bool previousLoaded = LFPG_GroupsStorage.LoadFile(finalPath, previousData, previousError);
            if (previousLoaded && ValidateGroupsData(previousData))
            {
                string backupError = "";
                if (FileExist(bakPath) && !ReadGroupsFile(bakPath, diskCount))
                {
                    if (!MoveAside(bakPath, ".corrupt", false))
                        backupError = "SaveGroups: cannot move invalid backup aside; continuing promotion.";
                }
                if (backupError == "" && FileExist(bakPath))
                {
                    if (!DeleteFile(bakPath))
                        backupError = "SaveGroups: cannot delete stale backup; continuing promotion.";
                }
                if (backupError == "" && !CopyFile(finalPath, bakPath))
                {
                    backupError = "SaveGroups: cannot copy final to backup; continuing promotion.";
                }
                if (backupError == "" && !GroupsFileMatches(bakPath, previousData))
                {
                    backupError = "SaveGroups: backup payload mismatch; continuing promotion.";
                }
                if (backupError != "")
                    LFPG_Log.Error(backupError);
            }
            else
            {
                if (!MoveAside(finalPath, ".corrupt"))
                {
                    LFPG_Log.Error("SaveGroups: final does not verify and cannot be moved aside. Rotation aborted.");
                    return false;
                }
                LFPG_Log.Error("SaveGroups: final did not verify; backup kept, final moved to groups.json.corrupt.");
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
                    return false;
                }
            }
            if (!CopyFile(tmpPath, finalPath))
            {
                LFPG_Log.Error("SaveGroups: cannot promote tmp to final. Verified tmp retained.");
                return false;
            }
        }

        // CopyFile success alone is not proof of a complete destination.
        if (!GroupsFileMatches(finalPath, fileData))
        {
            LFPG_Log.Error("SaveGroups: final payload mismatch. Verified tmp retained; save will retry.");
            return false;
        }

        m_HasVerifiedGroupsTmp = false;
        // 5. Limpiar tmp solo tras una rotacion completa.
        if (FileExist(tmpPath))
        {
            DeleteFile(tmpPath);
        }
        // Bounded to one retry: final now matches pendingData even if removing
        // tmp failed, so the next call writes the current in-memory state.
        if (retryPending)
            return SaveGroups();
        m_IsDirty = false;
        return true;
    }

    void LoadGroups()
    {
        if (m_GroupsLoadFailed)
            return;

        string filePath = LFPG_TerritoryConfig.GetGroupsPath();
        string bakPath = LFPG_TerritoryConfig.GetGroupsBackupPath();
        string dirPath = LFPG_TerritoryConfig.GetConfigDir();

        m_GroupsSourceMissing = !FileExist(filePath) && !FileExist(bakPath) && !FileExist(LFPG_TerritoryConfig.GetGroupsTmpPath());

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
            if (LFPG_GroupsStorage.LoadFile(filePath, fileData, loadError))
            {
                if (fileData.m_Version > LFPG_GROUPS_FILE_VERSION)
                {
                    m_GroupsLoadFailed = true;
                    LFPG_Log.Error("LoadGroups: newer groups format. READ ONLY; no fallback or rewrite.");
                    return;
                }
                // Un fichero valido con 0 grupos es un estado legitimo (se disolvio
                // el ultimo grupo). Tratarlo como corrupto hacia caer al backup y
                // RESUCITAR grupos ya borrados.
                if (fileData && fileData.m_Groups)
                    loaded = true;
            }
        }

        if (!loaded && FileExist(filePath))
        {
            m_GroupsLoadFailed = true;
            LFPG_Log.Error("LoadGroups: existing final invalid. READ ONLY; no fallback.");
            return;
        }

        // FIX PERS-1: el backup se intenta TAMBIEN cuando el primario no existe.
        // Ese es justo el estado que deja un crash durante la rotacion de SaveGroups.
        if (!loaded)
        {
            if (FileExist(bakPath))
            {
                fileData = new LFPG_GroupsFileData();
                if (LFPG_GroupsStorage.LoadFile(bakPath, fileData, loadError))
                {
                    if (ValidateGroupsData(fileData))
                    {
                        loaded = true;
                        if (!FileExist(filePath))
                        {
                            // Preserve the backup; the next successful save promotes it.
                            LFPG_Log.Info("groups.json missing; loaded verified backup (including zero groups).");
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
            if (!m_GroupsSourceMissing)
            {
                m_GroupsLoadFailed = true;
                LFPG_Log.Error("LoadGroups: no usable groups file. READ ONLY; restore groups.json and restart. No groups files will be written.");
            }
            else
            {
                LFPG_Log.Info("LoadGroups: no profile data. Waiting for restored flags before accepting a fresh world.");
                // Cover flags restored during super.OnInit, before this load.
                int missingIndex;
                for (missingIndex = 0; missingIndex < m_PendingFlags.Count(); missingIndex = missingIndex + 1)
                {
                    LFPG_FlagBase missingFlag = m_PendingFlags[missingIndex];
                    if (missingFlag && missingFlag.GetGroupID() != "")
                    {
                        m_GroupsLoadFailed = true;
                        m_DissolveDisabled = true;
                        LFPG_Log.Error("Owned flags restored without group profile. READ ONLY; restore profile and restart.");
                        break;
                    }
                }
            }
            return;
        }

        if (!ValidateGroupsData(fileData))
        {
            m_GroupsLoadFailed = true;
            LFPG_Log.Error("LoadGroups: unsupported version or invalid/duplicate group/member record. READ ONLY; no partial groups loaded.");
            return;
        }

        // Reconstruir estructuras en memoria con VALIDACION por grupo (FIX I-22)
        int i;
        int count = fileData.m_Groups.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_GroupData group = fileData.m_Groups[i];

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
                m_GroupNames.Set(GroupNameKey(group.m_GroupName), true);
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

        string logMsg = "Loaded ";
        logMsg = logMsg + m_Groups.Count().ToString();
        logMsg = logMsg + " groups from JSON.";
        LFPG_Log.Info(logMsg);
    }
};
