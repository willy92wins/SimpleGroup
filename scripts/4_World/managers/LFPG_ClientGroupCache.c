// ============================================================================
// LFPG_ClientGroupCache.c — 4_World/managers
// Cache client-side estático para checks rápidos sin RPC
//
// Se actualiza SOLO via RPC del server:
//  - Al conectar (LIGHTWEIGHT_SYNC)
//  - Al crear/unirse a grupo (GROUP_SYNC_FULL)
//  - Al cambiar estado de bandera
//
// NO se actualiza cada frame. Todos los campos son estáticos.
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

    // Datos de la bandera local
    static vector s_FlagPosition;
    static float s_FlagRaiseProgress;

    // Lista de miembros (para UI del panel)
    static ref array<ref LFPG_MemberData> s_Members;

    // Estado
    static bool s_HasGroup;

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
        s_FlagPosition = vector.Zero;
        s_FlagRaiseProgress = 0.0;
        s_HasGroup = false;
        if (!s_Members)
        {
            s_Members = new array<ref LFPG_MemberData>;
        }
        s_Members.Clear();
    }

    // ========================================================================
    // QUERIES — O(1), usadas por Hologram y UI
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

    // Check rápido de build zone — O(1), sin sqrt
    static bool IsInBuildZone(vector buildPos)
    {
        if (!s_HasGroup)
            return false;

        if (s_FlagRaiseProgress <= 0.0)
            return false;

        float dx = buildPos[0] - s_FlagPosition[0];
        float dz = buildPos[2] - s_FlagPosition[2];
        float distSq = (dx * dx) + (dz * dz);

        // Build radius: 30m → 900 squared
        // TODO: obtener de config si se sincroniza
        float radiusSq = 900.0;
        return (distSq < radiusSq);
    }

    static bool CanDeploy()
    {
        if (!s_HasGroup)
            return false;
        return (s_DeployedCount < s_DeployMax);
    }

    // ========================================================================
    // RPC HANDLERS — Llamados desde LFPG_FlagBase.OnRPC (client-side)
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
            ctx.Read(memberUID);
            ctx.Read(memberName);

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

        // Actualizar cache
        s_GroupID = groupID;
        s_GroupName = groupName;
        s_LeaderUID = leaderUID;
        s_Tier = tier;
        s_DeployedCount = deployedCount;
        s_DeployMax = deployLimit;
        s_GardenPlotCount = gardenCount;
        s_FlagPosition = flagPos;
        s_HasGroup = true;

        // Actualizar raise progress desde la bandera si está disponible
        if (flag)
        {
            s_FlagRaiseProgress = flag.m_RaiseProgressNet;
        }

        // Notificar al panel si está abierto
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

        // Actualizar cache
        s_GroupID = groupID;
        s_GroupName = groupName;
        s_LeaderUID = leaderUID;
        s_Tier = tier;
        s_DeployedCount = deployedCount;
        s_DeployMax = deployLimit;
        s_FlagPosition = flagPos;
        s_FlagRaiseProgress = raiseProgress;
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

        // Abrir el diálogo de nombre con referencia a la bandera para RPCs
        LFPG_GroupNameDialog.Open(groupID, flag);
    }

    protected static void HandleNameResult(ParamsReadContext ctx)
    {
        int result = 0;
        ctx.Read(result);

        LFPG_GroupNameDialog.HandleNameResult(result);
    }
};
