// ============================================================================
// LFPG_ClientGroupCache.c - 4_World/managers
// Cache client-side estatico para checks rapidos sin RPC
//
// Se actualiza SOLO via RPC del server:
//  - Al conectar (LIGHTWEIGHT_SYNC)
//  - Al crear/unirse a grupo (GROUP_SYNC_FULL)
//  - Al cambiar estado de bandera
//
// NO se actualiza cada frame. Todos los campos son estaticos.
// Usado por el modded Hologram para check O(1) de build zone.
// ============================================================================

class LFPG_ClientGroupCache
{
    // Datos del grupo local
    static string s_GroupID;
    static string s_GroupName;
    static string s_LeaderUID;
    static int s_Tier;
    static int s_DeployedCount;
    static int s_DeployMax;
    static int s_GardenPlotCount;

    // FIX M4: Max garden plots sincronizado desde server
    static int s_GardenPlotMax;

    // Datos de la bandera local
    static vector s_FlagPosition;
    static float s_FlagRaiseProgress;

    // Lista de miembros (para UI del panel)
    static ref array<ref LFPG_MemberData> s_Members;

    // Estado
    static bool s_HasGroup;

    // FIX 4: Build radius sincronizado desde server
    static float s_BuildRadiusSq;

    // ========================================================================
    // INIT / CLEAR
    // ========================================================================
    static void Init()
    {
        Clear();
    }

    static void Clear()
    {
        s_GroupID = "";
        s_GroupName = "";
        s_LeaderUID = "";
        s_Tier = 0;
        s_DeployedCount = 0;
        s_DeployMax = 0;
        s_GardenPlotCount = 0;
        s_GardenPlotMax = 3;
        s_FlagPosition = vector.Zero;
        s_FlagRaiseProgress = 0.0;
        s_HasGroup = false;
        s_BuildRadiusSq = 900.0;
        if (!s_Members)
        {
            s_Members = new array<ref LFPG_MemberData>;
        }
        s_Members.Clear();
    }

    // ========================================================================
    // QUERIES - O(1), usadas por Hologram y UI
    // ========================================================================
    static bool HasGroup()
    {
        return s_HasGroup;
    }

    static bool IsLeader()
    {
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return false;
        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return false;
        return (identity.GetPlainId() == s_LeaderUID);
    }

    // Check rapido de build zone - O(1), sin sqrt
    // FIX 4: usa s_BuildRadiusSq sincronizado en vez de hardcoded
    static bool IsInBuildZone(vector buildPos)
    {
        if (!s_HasGroup)
            return false;

        if (s_FlagRaiseProgress <= 0.0)
            return false;

        float dx = buildPos[0] - s_FlagPosition[0];
        float dz = buildPos[2] - s_FlagPosition[2];
        float distSq = (dx * dx) + (dz * dz);

        return (distSq < s_BuildRadiusSq);
    }

    // FIX 3: Verifica si una entidad bandera en el mundo es la de nuestro grupo
    // Compara posicion con la cacheada (tolerance 2m = 4.0 distSq)
    static bool IsFlagAtPosition(vector flagPos)
    {
        if (!s_HasGroup)
            return false;

        float dx = flagPos[0] - s_FlagPosition[0];
        float dz = flagPos[2] - s_FlagPosition[2];
        float distSq = (dx * dx) + (dz * dz);
        return (distSq < 4.0);
    }

    static bool CanDeploy()
    {
        if (!s_HasGroup)
            return false;
        return (s_DeployedCount < s_DeployMax);
    }

    // FIX M4: Check de garden limit client-side
    static bool CanPlaceGarden()
    {
        if (!s_HasGroup)
            return false;
        return (s_GardenPlotCount < s_GardenPlotMax);
    }

    // C1 FIX: Helper centralizado para buscar la bandera del grupo local
    // Usa IsFlagAtPosition (posicion cacheada) en vez de GetGroupID (no sincronizado)
    // Retorna null si no se encuentra en 100m o no tiene grupo
    static LFPG_FlagBase FindLocalGroupFlag()
    {
        if (!s_HasGroup)
            return null;

        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return null;

        vector playerPos = player.GetPosition();
        float searchRadius = 100.0;
        array<Object> objects = new array<Object>;
        array<CargoBase> proxyCargos = new array<CargoBase>;
        GetGame().GetObjectsAtPosition(playerPos, searchRadius, objects, proxyCargos);

        int i;
        int count = objects.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_FlagBase flag = LFPG_FlagBase.Cast(objects[i]);
            if (flag && IsFlagAtPosition(flag.GetPosition()))
            {
                return flag;
            }
        }
        return null;
    }

    // ========================================================================
    // RPC HANDLERS - Llamados desde LFPG_FlagBase.OnRPC (client-side)
    // ========================================================================
    static void HandleClientRPC(int rpc_type, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        if (rpc_type == LFPG_RPC_S2C_GROUP_SYNC_FULL)
        {
            HandleGroupSyncFull(ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_S2C_LIGHTWEIGHT_SYNC)
        {
            HandleLightweightSync(ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_S2C_GROUP_DISSOLVED)
        {
            HandleGroupDissolved(ctx);
        }
        else if (rpc_type == LFPG_RPC_S2C_OPEN_NAME_DIALOG)
        {
            HandleOpenNameDialog(ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_S2C_NAME_RESULT)
        {
            HandleNameResult(ctx);
        }
    }

    protected static void HandleGroupSyncFull(ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string groupID = "";
        if (!ctx.Read(groupID))
            return;

        string groupName = "";
        if (!ctx.Read(groupName))
            return;

        string leaderUID = "";
        if (!ctx.Read(leaderUID))
            return;

        int tier = 1;
        if (!ctx.Read(tier))
            return;

        int deployedCount = 0;
        if (!ctx.Read(deployedCount))
            return;

        int gardenCount = 0;
        if (!ctx.Read(gardenCount))
            return;

        int memberCount = 0;
        if (!ctx.Read(memberCount))
            return;

        // Leer miembros y almacenar para la UI
        if (!s_Members)
        {
            s_Members = new array<ref LFPG_MemberData>;
        }
        s_Members.Clear();

        int i;
        for (i = 0; i < memberCount; i = i + 1)
        {
            string memberUID = "";
            string memberName = "";
            if (!ctx.Read(memberUID))
                return;
            if (!ctx.Read(memberName))
                return;

            LFPG_MemberData md = new LFPG_MemberData();
            md.m_PlayerUID = memberUID;
            md.m_PlayerName = memberName;
            s_Members.Insert(md);
        }

        int deployLimit = 8;
        if (!ctx.Read(deployLimit))
            return;

        vector flagPos = vector.Zero;
        if (!ctx.Read(flagPos))
            return;

        // FIX 4: Build radius sincronizado
        float buildRadiusSq = 900.0;
        ctx.Read(buildRadiusSq);

        // FIX M4: Garden max sincronizado
        int gardenMax = 3;
        ctx.Read(gardenMax);

        // Actualizar cache
        s_GroupID = groupID;
        s_GroupName = groupName;
        s_LeaderUID = leaderUID;
        s_Tier = tier;
        s_DeployedCount = deployedCount;
        s_DeployMax = deployLimit;
        s_GardenPlotCount = gardenCount;
        s_GardenPlotMax = gardenMax;
        s_FlagPosition = flagPos;
        s_BuildRadiusSq = buildRadiusSq;
        s_HasGroup = true;

        // Actualizar raise progress desde la bandera si esta disponible
        if (flag)
        {
            s_FlagRaiseProgress = flag.m_RaiseProgressNet;
        }

        // Notificar al panel si esta abierto
        LFPG_GroupPanel panel = LFPG_GroupPanel.GetInstance();
        if (panel)
        {
            panel.OnDataReceived();
        }
    }

    protected static void HandleLightweightSync(ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string groupID = "";
        if (!ctx.Read(groupID))
            return;

        string groupName = "";
        if (!ctx.Read(groupName))
            return;

        string leaderUID = "";
        if (!ctx.Read(leaderUID))
            return;

        int tier = 1;
        if (!ctx.Read(tier))
            return;

        int deployedCount = 0;
        if (!ctx.Read(deployedCount))
            return;

        int deployLimit = 8;
        if (!ctx.Read(deployLimit))
            return;

        vector flagPos = vector.Zero;
        if (!ctx.Read(flagPos))
            return;

        float raiseProgress = 0.0;
        if (!ctx.Read(raiseProgress))
            return;

        // FIX 4: Build radius sincronizado
        float buildRadiusSq = 900.0;
        ctx.Read(buildRadiusSq);

        // FIX AUDIT: Garden count/max en lightweight sync
        int gardenCountLW = 0;
        ctx.Read(gardenCountLW);
        int gardenMaxLW = 3;
        ctx.Read(gardenMaxLW);

        // Actualizar cache
        s_GroupID = groupID;
        s_GroupName = groupName;
        s_LeaderUID = leaderUID;
        s_Tier = tier;
        s_DeployedCount = deployedCount;
        s_DeployMax = deployLimit;
        s_FlagPosition = flagPos;
        s_FlagRaiseProgress = raiseProgress;
        s_BuildRadiusSq = buildRadiusSq;
        s_GardenPlotCount = gardenCountLW;
        s_GardenPlotMax = gardenMaxLW;
        s_HasGroup = true;
    }

    protected static void HandleGroupDissolved(ParamsReadContext ctx)
    {
        string groupID = "";
        ctx.Read(groupID);

        // Solo limpiar si es nuestro grupo
        if (groupID == s_GroupID)
        {
            Clear();
        }
    }

    protected static void HandleOpenNameDialog(ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string groupID = "";
        ctx.Read(groupID);

        // Abrir el dialogo de nombre con referencia a la bandera para RPCs
        LFPG_GroupNameDialog.Open(groupID, flag);
    }

    protected static void HandleNameResult(ParamsReadContext ctx)
    {
        int result = 0;
        ctx.Read(result);

        LFPG_GroupNameDialog.HandleNameResult(result);
    }
};
