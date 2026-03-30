// ============================================================================
// LFPG_GroupManager.c — 4_World/managers
// Singleton SERVER-SIDE — el corazón del sistema de grupos
//
// OPTIMIZACIONES:
//  - m_PlayerToGroup: O(1) lookup para IsInBuildZone (hot path)
//  - m_GroupNames: O(1) validación de nombres únicos
//  - m_FlagPositions: array para territory overlap (no GetObjectsAtPosition)
//  - Sin m_FlagToGroup (redundante: flag.m_GroupID es suficiente)
//  - Timer class para tick periódico (inmune al bug CallLater 4.5h)
//  - JSON save atómico (tmp → bak → final)
//  - RPC rate limiting por jugador
//  - Deploy/Garden counters O(1) (no proximity scan en runtime)
// ============================================================================

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

    // Primary storage: groupID → GroupData
    protected ref map<string, ref LFPG_GroupData> m_Groups;

    // Fast lookups
    protected ref map<string, string> m_PlayerToGroup;
    protected ref array<string> m_GroupNames;

    // Flag position cache — for territory overlap checks
    ref array<ref LFPG_FlagPositionCache> m_FlagPositions;

    // Flag entity references (groupID → flag) — runtime only
    protected ref map<string, LFPG_FlagBase> m_GroupFlags;

    // RPC rate limiting: playerUID → last RPC time (ms)
    protected ref map<string, int> m_RPCThrottle;

    // Periodic validation timer (Timer class, NOT CallLater)
    protected ref Timer m_ValidationTimer;

    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================
    void LFPG_GroupManager()
    {
        m_Groups = new map<string, ref LFPG_GroupData>;
        m_PlayerToGroup = new map<string, string>;
        m_GroupNames = new array<string>;
        m_FlagPositions = new array<ref LFPG_FlagPositionCache>;
        m_GroupFlags = new map<string, LFPG_FlagBase>;
        m_RPCThrottle = new map<string, int>;
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
    // INIT — Llamado desde modded MissionServer.OnInit()
    // ========================================================================
    void Init()
    {
        // Cargar config
        m_Config = LFPG_TerritoryConfig.Load();

        // Cargar grupos desde JSON
        LoadGroups();

        // Timer periódico de validación (cada 5 min)
        // Usa Timer class (NO CallLater) — inmune al bug de 4.5h
        m_ValidationTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        m_ValidationTimer.Run(300.0, this, "OnValidationTick", null, true);

        string msg = "[LFPG_Territory] GroupManager initialized. Groups: ";
        msg = msg + m_Groups.Count().ToString();
        Print(msg);
    }

    LFPG_TerritoryConfig GetConfig()
    {
        return m_Config;
    }

    // ========================================================================
    // PERIODIC VALIDATION (cada 5 min)
    // NO recalcula raiseProgress (eso es lazy).
    // Solo valida integridad: grupos sin bandera → disolver.
    // ========================================================================
    void OnValidationTick()
    {
        // Verificar que cada grupo tiene su bandera registrada
        array<string> orphanGroups = new array<string>;
        int i;
        int groupCount = m_Groups.Count();

        // Iterar grupos buscando huérfanos
        for (i = 0; i < groupCount; i = i + 1)
        {
            string groupID = m_Groups.GetKey(i);
            if (!m_GroupFlags.Contains(groupID))
            {
                orphanGroups.Insert(groupID);
            }
            else
            {
                LFPG_FlagBase flag = m_GroupFlags.Get(groupID);
                if (!flag)
                {
                    orphanGroups.Insert(groupID);
                }
            }
        }

        // Disolver huérfanos
        int orphanCount = orphanGroups.Count();
        for (i = 0; i < orphanCount; i = i + 1)
        {
            string orphanID = orphanGroups[i];
            string warnMsg = "[LFPG_Territory] Orphan group detected, dissolving: ";
            warnMsg = warnMsg + orphanID;
            Print(warnMsg);
            DissolveGroup(orphanID);
        }
    }

    // ========================================================================
    // FLAG REGISTRATION — Llamado desde flag.AfterStoreLoad() y al crear grupo
    // ========================================================================
    void RegisterFlag(LFPG_FlagBase flag, string groupID)
    {
        if (!flag || groupID == "")
            return;

        m_GroupFlags.Set(groupID, flag);
        UpdateFlagPositionCache(groupID, flag.GetPosition(), flag.ComputeCurrentRaiseProgress(), flag.GetTier());
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
    // FLAG POSITION CACHE — Para territory overlap checks
    // Array lineal: con 50 flags, 50 comparaciones es despreciable
    // Mucho más barato que GetObjectsAtPosition(500m)
    // ========================================================================
    protected void UpdateFlagPositionCache(string groupID, vector pos, float progress, int tier)
    {
        int i;
        int count = m_FlagPositions.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_FlagPositionCache cache = m_FlagPositions[i];
            if (cache && cache.m_GroupID == groupID)
            {
                cache.Set(pos, groupID, progress, tier);
                return;
            }
        }

        // No existe — añadir nuevo
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
    // HOT PATH: IsInBuildZone — O(1) — Llamado cada frame por hologram
    // ========================================================================
    bool IsInBuildZone(string playerUID, vector buildPos)
    {
        // 1. Lookup grupo del jugador — O(1)
        if (!m_PlayerToGroup.Contains(playerUID))
            return false;

        string groupID = m_PlayerToGroup.Get(playerUID);
        if (groupID == "")
            return false;

        // 2. Obtener datos del grupo — O(1)
        if (!m_Groups.Contains(groupID))
            return false;

        // 3. Obtener posición de la bandera desde cache — O(f) peor caso
        //    pero típicamente el jugador tiene UNA bandera, así que es O(1) amortizado
        LFPG_FlagBase flag = null;
        if (m_GroupFlags.Contains(groupID))
        {
            flag = m_GroupFlags.Get(groupID);
        }

        if (!flag)
            return false;

        // 4. Check distancia sin sqrt — O(1)
        vector flagPos = flag.GetPosition();
        float distSq = vector.DistanceSq(buildPos, flagPos);
        if (distSq > m_Config.m_BuildRadiusSq)
            return false;

        // 5. Check bandera levantada (lazy compute)
        float progress = flag.ComputeCurrentRaiseProgress();
        if (progress <= 0.0)
            return false;

        return true;
    }

    // ========================================================================
    // TERRITORY CHECK — ¿Hay otra bandera a <500m de esta posición?
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
                float distSq = vector.DistanceSq(pos, cache.m_Position);
                if (distSq < m_Config.m_TerritoryRadiusSq)
                    return true;
            }
        }
        return false;
    }

    // ========================================================================
    // DEPLOY / GARDEN COUNTERS — O(1) runtime
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
            SaveGroups();
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
            SaveGroups();
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
            SaveGroups();
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
            SaveGroups();
        }
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

        if (m_GroupNames.Find(groupName) >= 0)
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

        // Añadir líder como primer miembro
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
        m_GroupNames.Insert(groupName);

        // Configurar la bandera
        flag.SetGroupID(groupID);
        flag.SetMemberCount(1);
        RegisterFlag(flag, groupID);

        // Persistir
        SaveGroups();

        string logMsg = "[LFPG_Territory] Group created: ";
        logMsg = logMsg + groupName;
        logMsg = logMsg + " (";
        logMsg = logMsg + groupID;
        logMsg = logMsg + ") by ";
        logMsg = logMsg + playerUID;
        Print(logMsg);

        return groupID;
    }

    // Disolver grupo: se llama al destruir bandera o grupo huérfano
    void DissolveGroup(string groupID)
    {
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

        // Limpiar nombre
        int nameIdx = m_GroupNames.Find(group.m_GroupName);
        if (nameIdx >= 0)
        {
            m_GroupNames.Remove(nameIdx);
        }

        // Destruir objetos desplegados si la config lo indica
        // ANTES de UnregisterFlag — DestroyDeployedObjects usa GetGroupFlag()
        if (m_Config && m_Config.m_DestroyDeployedOnDissolve)
        {
            DestroyDeployedObjects(group);
        }

        // Limpiar flag references (DESPUÉS de destruir deployed objects)
        UnregisterFlag(groupID);

        // Eliminar grupo
        m_Groups.Remove(groupID);

        SaveGroups();

        string logMsg = "[LFPG_Territory] Group dissolved: ";
        logMsg = logMsg + groupID;
        Print(logMsg);
    }

    // Añadir miembro
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

        LFPG_MemberData member = new LFPG_MemberData();
        member.Set(playerUID, playerName, GetGame().GetTime());
        group.m_Members.Insert(member);

        m_PlayerToGroup.Set(playerUID, groupID);

        // Actualizar SyncVar en la bandera
        LFPG_FlagBase flag = GetGroupFlag(groupID);
        if (flag)
        {
            flag.SetMemberCount(group.GetMemberCount());
        }

        SaveGroups();

        // Notificar a todos los miembros online
        SendGroupSyncUpdateToMembers(group, LFPG_SYNC_MEMBER_ADDED);

        return true;
    }

    // Quitar miembro — con traspaso de liderazgo si es líder que abandona
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

        // Quitar del lookup rápido
        if (m_PlayerToGroup.Contains(playerUID))
        {
            m_PlayerToGroup.Remove(playerUID);
        }

        // Si era el líder Y quedan miembros → traspasar liderazgo
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

        // Si no quedan miembros → disolver grupo
        if (group.GetMemberCount() <= 0)
        {
            DissolveGroup(groupID);
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

    // Transferir liderazgo (solo por petición voluntaria del líder)
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

    // ¿Tiene este jugador un grupo?
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
    // UPGRADE — Swap de entidad de bandera
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

        // Spawn nueva bandera — verificar que se creó correctamente
        Object obj = GetGame().CreateObjectEx(newClassName, pos, ECE_PLACE_ON_SURFACE);
        LFPG_FlagBase newFlag = LFPG_FlagBase.Cast(obj);
        if (!newFlag)
        {
            // ABORT — no borrar vieja, no modificar nada
            string errMsg = "[LFPG_Territory] ERROR: Failed to spawn upgrade entity: ";
            errMsg = errMsg + newClassName;
            Print(errMsg);
            return false;
        }

        // Configurar nueva bandera
        newFlag.SetOrientation(ori);
        newFlag.TransferDataFrom(oldFlag);
        newFlag.SetMemberCount(memberCount);

        // Actualizar tier en grupo
        group.m_Tier = newFlag.GetTier();

        // Actualizar NetworkID
        int netLow = 0;
        int netHigh = 0;
        newFlag.GetNetworkID(netLow, netHigh);
        group.m_FlagNetLow = netLow;
        group.m_FlagNetHigh = netHigh;

        // Registrar nueva bandera, desregistrar vieja
        UnregisterFlag(groupID);
        RegisterFlag(newFlag, groupID);

        // Borrar entidad vieja (DESPUÉS de registrar la nueva)
        GetGame().ObjectDelete(oldFlag);

        // Persistir
        SaveGroups();

        // Notificar miembros
        SendGroupSyncUpdateToMembers(group, LFPG_SYNC_TIER_CHANGED);

        string logMsg = "[LFPG_Territory] Flag upgraded: group=";
        logMsg = logMsg + groupID;
        logMsg = logMsg + " newTier=";
        logMsg = logMsg + newFlag.GetTier().ToString();
        Print(logMsg);

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

        // Check nombre duplicado
        if (m_GroupNames.Find(name) >= 0)
            return LFPG_NAME_TAKEN;

        return LFPG_NAME_OK;
    }

    // ========================================================================
    // RPC RATE LIMITING — Anti-spam
    // ========================================================================
    protected bool IsRPCThrottled(string playerUID)
    {
        int now = GetGame().GetTime();
        if (m_RPCThrottle.Contains(playerUID))
        {
            int lastTime = m_RPCThrottle.Get(playerUID);
            int diff = now - lastTime;
            if (diff < LFPG_RPC_THROTTLE_MS)
                return true;
        }
        m_RPCThrottle.Set(playerUID, now);
        return false;
    }

    // ========================================================================
    // RPC HANDLER — Server-side dispatcher
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
            HandleCreateGroup(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_SET_GROUP_NAME)
        {
            HandleSetGroupName(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_REQUEST_JOIN)
        {
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
            HandleStartInvite(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_DESTROY_FLAG)
        {
            HandleDestroyFlag(sender, ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_C2S_REQUEST_GROUP_DATA)
        {
            HandleRequestGroupData(sender, ctx, flag);
        }
    }

    // ========================================================================
    // RPC HANDLERS — Individual operations
    // ========================================================================

    protected void HandleCreateGroup(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string senderUID = sender.GetPlainId();
        string senderName = sender.GetName();

        // Validar: jugador sin grupo
        if (HasGroup(senderUID))
            return;

        // La bandera no debe tener grupo ya asignado
        if (flag && flag.HasGroup())
            return;

        // Crear grupo con nombre temporal (se renombra via SET_GROUP_NAME)
        string tempName = "Group_";
        tempName = tempName + senderUID.Substring(senderUID.Length() - 4, 4);

        string groupID = CreateGroup(senderUID, senderName, tempName, flag);
        if (groupID == "")
            return;

        // Pedir al cliente que abra el diálogo de nombre
        SendOpenNameDialog(sender, flag, groupID);

        // Enviar sync completo al creador
        SendGroupSyncFull(sender, groupID, flag);
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

        // Solo el líder puede renombrar
        if (!group.IsLeader(senderUID))
            return;

        // Validar nombre
        int result = ValidateGroupName(newName);
        if (result != LFPG_NAME_OK)
        {
            SendNameResult(sender, flag, result);
            return;
        }

        // Quitar nombre viejo del set
        int oldIdx = m_GroupNames.Find(group.m_GroupName);
        if (oldIdx >= 0)
        {
            m_GroupNames.Remove(oldIdx);
        }

        // Asignar nuevo nombre
        group.m_GroupName = newName;
        m_GroupNames.Insert(newName);

        SaveGroups();

        SendNameResult(sender, flag, LFPG_NAME_OK);
        SendGroupSyncUpdateToMembers(group, LFPG_SYNC_LEADER_CHANGED);
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

        string groupID = flag.GetGroupID();
        if (groupID == "")
            return;

        bool added = AddMember(groupID, senderUID, senderName);
        if (added)
        {
            SendGroupSyncFull(sender, groupID, flag);
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
        string senderUID = sender.GetPlainId();
        string groupID = GetPlayerGroupID(senderUID);
        if (groupID == "")
            return;

        if (!flag)
            return;

        // Verificar que la bandera pertenece al grupo
        if (flag.GetGroupID() != groupID)
            return;

        // Activar invite mode
        int durationMs = m_Config.m_InviteDurationSeconds * 1000;
        flag.ActivateInviteMode(durationMs);
    }

    protected void HandleDestroyFlag(PlayerIdentity sender, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string senderUID = sender.GetPlainId();
        string groupID = GetPlayerGroupID(senderUID);
        if (groupID == "")
            return;

        LFPG_GroupData group = m_Groups.Get(groupID);
        if (!group || !group.IsLeader(senderUID))
            return;

        if (!flag || flag.GetGroupID() != groupID)
            return;

        // Verificar que el jugador tiene hatchet en manos
        PlayerBase player = PlayerBase.Cast(sender.GetPlayer());
        if (!player)
            return;

        EntityAI itemInHands = player.GetHumanInventory().GetEntityInHands();
        if (!itemInHands)
            return;

        string itemType = itemInHands.GetType();
        if (itemType != "Hatchet")
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

        SendGroupSyncFull(sender, groupID, flag);
    }

    // ========================================================================
    // PLAYER JOIN/LEAVE SERVER — Llamado desde modded PlayerBase
    // ========================================================================
    void OnPlayerJoined(string playerUID, PlayerBase player)
    {
        if (!HasGroup(playerUID))
            return;

        // Enviar lightweight sync para que el cliente tenga datos de cache
        // antes de intentar construir
        string groupID = GetPlayerGroupID(playerUID);
        LFPG_FlagBase flag = GetGroupFlag(groupID);

        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return;

        SendLightweightSync(identity, groupID, flag);
    }

    // ========================================================================
    // SEND RPC HELPERS — Server → Client
    // ========================================================================

    void SendGroupSyncFull(PlayerIdentity target, string groupID, LFPG_FlagBase flag)
    {
        if (!target || !flag)
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

        // Miembros
        int memberCount = group.GetMemberCount();
        rpc.Write(memberCount);
        int i;
        for (i = 0; i < memberCount; i = i + 1)
        {
            LFPG_MemberData member = group.m_Members[i];
            if (member)
            {
                rpc.Write(member.m_PlayerUID);
                rpc.Write(member.m_PlayerName);
            }
        }

        // Deploy limit para este tier
        int deployLimit = group.GetDeployLimit(m_Config);
        rpc.Write(deployLimit);

        // Posición de la bandera (para cache del cliente)
        vector flagPos = flag.GetPosition();
        rpc.Write(flagPos);

        rpc.Send(flag, LFPG_RPC_S2C_GROUP_SYNC_FULL, true, target);
    }

    void SendLightweightSync(PlayerIdentity target, string groupID, LFPG_FlagBase flag)
    {
        if (!target)
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

        int deployLimit = group.GetDeployLimit(m_Config);
        rpc.Write(deployLimit);

        if (flag)
        {
            rpc.Write(flag.GetPosition());
            rpc.Write(flag.ComputeCurrentRaiseProgress());
        }
        else
        {
            rpc.Write(vector.Zero);
            rpc.Write(0.0);
        }

        // Se envía a null target (flag puede no existir aún en cliente)
        // El cliente lo procesa via PlayerBase.OnRPC o MissionGameplay
        if (flag)
        {
            rpc.Send(flag, LFPG_RPC_S2C_LIGHTWEIGHT_SYNC, true, target);
        }
    }

    protected void SendGroupSyncUpdateToMembers(LFPG_GroupData group, int updateType)
    {
        if (!group)
            return;

        int i;
        int count = group.m_Members.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_MemberData member = group.m_Members[i];
            if (!member)
                continue;

            PlayerBase player = GetPlayerByUID(member.m_PlayerUID);
            if (!player)
                continue;

            PlayerIdentity identity = player.GetIdentity();
            if (!identity)
                continue;

            LFPG_FlagBase flag = GetGroupFlag(group.m_GroupID);
            SendGroupSyncFull(identity, group.m_GroupID, flag);
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

        // Buscar cualquier bandera cercana como target del RPC
        // Si no hay, no podemos enviar (el jugador lo detectará al abrir panel)
        LFPG_FlagBase flag = GetGroupFlag(groupID);
        if (!flag)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(groupID);
        rpc.Send(flag, LFPG_RPC_S2C_GROUP_DISSOLVED, true, identity);
    }

    protected void SendOpenNameDialog(PlayerIdentity target, LFPG_FlagBase flag, string groupID)
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

    // ========================================================================
    // UTILITY
    // ========================================================================

    protected PlayerBase GetPlayerByUID(string uid)
    {
        // Buscar jugador online por UID
        array<Man> players = new array<Man>;
        GetGame().GetPlayers(players);
        int i;
        int count = players.Count();
        for (i = 0; i < count; i = i + 1)
        {
            Man man = players[i];
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

    protected void DestroyDeployedObjects(LFPG_GroupData group)
    {
        // Buscar objetos construidos en el radio de la bandera
        LFPG_FlagBase flag = GetGroupFlag(group.m_GroupID);
        if (!flag)
            return;

        vector pos = flag.GetPosition();
        float radius = m_Config.m_BuildRadiusMeters;

        array<Object> objects = new array<Object>;
        array<CargoBase> proxyCargos = new array<CargoBase>;
        GetGame().GetObjectsAtPosition(pos, radius, objects, proxyCargos);

        int i;
        int count = objects.Count();
        for (i = 0; i < count; i = i + 1)
        {
            Object obj = objects[i];
            if (!obj)
                continue;

            // Solo destruir objetos de basebuilding
            BaseBuildingBase building = BaseBuildingBase.Cast(obj);
            if (building)
            {
                GetGame().ObjectDelete(building);
            }
        }
    }

    // ========================================================================
    // PERSISTENCE — JSON atómico
    // ========================================================================
    void SaveGroups()
    {
        LFPG_GroupsFileData fileData = new LFPG_GroupsFileData();
        fileData.m_Version = 1;

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

        // Escritura atómica: tmp → bak → final
        string tmpPath = LFPG_TerritoryConfig.GetGroupsTmpPath();
        string bakPath = LFPG_TerritoryConfig.GetGroupsBackupPath();
        string finalPath = LFPG_TerritoryConfig.GetGroupsPath();

        // 1. Escribir a tmp
        JsonFileLoader<LFPG_GroupsFileData>.JsonSaveFile(tmpPath, fileData);

        // 2. Backup del actual si existe
        if (FileExist(finalPath))
        {
            if (FileExist(bakPath))
            {
                DeleteFile(bakPath);
            }
            CopyFile(finalPath, bakPath);
        }

        // 3. Mover tmp a final
        if (FileExist(finalPath))
        {
            DeleteFile(finalPath);
        }
        CopyFile(tmpPath, finalPath);

        // 4. Limpiar tmp
        if (FileExist(tmpPath))
        {
            DeleteFile(tmpPath);
        }
    }

    void LoadGroups()
    {
        string filePath = LFPG_TerritoryConfig.GetGroupsPath();
        string dirPath = LFPG_TerritoryConfig.GetConfigDir();

        // Crear directorio si no existe
        if (!FileExist(dirPath))
        {
            MakeDirectory(dirPath);
        }

        if (!FileExist(filePath))
        {
            Print("[LFPG_Territory] No groups.json found. Starting fresh.");
            return;
        }

        LFPG_GroupsFileData fileData = new LFPG_GroupsFileData();
        JsonFileLoader<LFPG_GroupsFileData>.JsonLoadFile(filePath, fileData);

        if (!fileData || !fileData.m_Groups)
        {
            Print("[LFPG_Territory] ERROR: Failed to parse groups.json");
            return;
        }

        // Reconstruir estructuras en memoria
        int i;
        int count = fileData.m_Groups.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_GroupData group = fileData.m_Groups[i];
            if (!group || group.m_GroupID == "")
                continue;

            m_Groups.Set(group.m_GroupID, group);

            // Reconstruir nombre set
            if (group.m_GroupName != "")
            {
                m_GroupNames.Insert(group.m_GroupName);
            }

            // Reconstruir player→group map
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

        string logMsg = "[LFPG_Territory] Loaded ";
        logMsg = logMsg + m_Groups.Count().ToString();
        logMsg = logMsg + " groups from JSON.";
        Print(logMsg);
    }
};
