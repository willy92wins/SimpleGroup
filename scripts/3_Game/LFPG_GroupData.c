// ============================================================================
// LFPG_GroupData.c - 3_Game
// Modelo de datos serializable a JSON para grupos y miembros
//
// OPTIMIZACIONES aplicadas:
//  - Sin m_DeployedObjectIDs (array de tracking eliminado)
//  - m_DeployedCount / m_GardenPlotCount como counters O(1)
//  - Se recalibran en startup con scan de proximidad una sola vez
//  - [NonSerialized] en campos runtime-only para no contaminar JSON
// ============================================================================

// ----------------------------------------------------------------------------
// LFPG_MemberData - Datos de un miembro del grupo (JSON-serializable)
// ----------------------------------------------------------------------------
class LFPG_MemberData
{
    string m_PlayerUID;
    string m_PlayerName;
    int m_JoinTimestamp;

    void LFPG_MemberData()
    {
        m_PlayerUID = "";
        m_PlayerName = "";
        m_JoinTimestamp = 0;
    }

    void Set(string uid, string name, int timestamp)
    {
        m_PlayerUID = uid;
        m_PlayerName = name;
        m_JoinTimestamp = timestamp;
    }
};

// ----------------------------------------------------------------------------
// LFPG_GroupData - Datos completos de un grupo (JSON-serializable)
// ----------------------------------------------------------------------------
class LFPG_GroupData
{
    // --- Campos persistidos en groups.json ---
    string m_GroupID;
    string m_GroupName;
    string m_LeaderUID;
    ref array<ref LFPG_MemberData> m_Members;
    int m_Tier;
    int m_DeployedCount;
    int m_GardenPlotCount;

    // --- Campos runtime-only (NO se guardan en JSON) ---
    [NonSerialized()]
    int m_FlagNetLow;
    [NonSerialized()]
    int m_FlagNetHigh;

    void LFPG_GroupData()
    {
        m_GroupID = "";
        m_GroupName = "";
        m_LeaderUID = "";
        m_Members = new array<ref LFPG_MemberData>;
        m_Tier = 1;
        m_DeployedCount = 0;
        m_GardenPlotCount = 0;
        m_FlagNetLow = 0;
        m_FlagNetHigh = 0;
    }

    // Genera un ID unico: timestamp del server + random + sufijo del UID
    static string GenerateGroupID(string leaderUID)
    {
        int gameTime = GetGame().GetTime();
        int rnd = Math.RandomInt(1000, 999999);
        string id = "grp_";
        id = id + gameTime.ToString();
        id = id + "_";
        id = id + rnd.ToString();
        id = id + "_";
        // Tomar ultimos 6 chars del UID para unicidad adicional
        int uidLen = leaderUID.Length();
        if (uidLen > 6)
        {
            int startIdx = uidLen - 6;
            id = id + leaderUID.Substring(startIdx, 6);
        }
        else
        {
            id = id + leaderUID;
        }
        return id;
    }

    // Busca un miembro por UID. Retorna index o -1
    int FindMemberIndex(string playerUID)
    {
        int i;
        int count = m_Members.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_MemberData member = m_Members[i];
            if (member && member.m_PlayerUID == playerUID)
            {
                return i;
            }
        }
        return -1;
    }

    // ?Es este jugador miembro del grupo?
    bool IsMember(string playerUID)
    {
        return (FindMemberIndex(playerUID) >= 0);
    }

    // ?Es este jugador el lider?
    bool IsLeader(string playerUID)
    {
        return (m_LeaderUID == playerUID);
    }

    // Obtiene el miembro mas antiguo que NO sea el lider actual
    // Para traspaso de liderazgo al abandonar
    string GetOldestMemberUID()
    {
        int i;
        int count = m_Members.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_MemberData member = m_Members[i];
            if (member && member.m_PlayerUID != m_LeaderUID)
            {
                return member.m_PlayerUID;
            }
        }
        return "";
    }

    // Numero de miembros
    int GetMemberCount()
    {
        return m_Members.Count();
    }

    // Obtiene el deploy limit para el tier actual
    int GetDeployLimit(LFPG_TerritoryConfig config)
    {
        if (!config || !config.m_TierDeployLimits)
            return 8;

        int idx = m_Tier - 1;
        if (idx < 0)
            idx = 0;
        if (idx >= config.m_TierDeployLimits.Count())
            idx = config.m_TierDeployLimits.Count() - 1;

        return config.m_TierDeployLimits[idx];
    }
};

// ----------------------------------------------------------------------------
// LFPG_FlagPositionCache - Struct ligera para cache de posiciones de banderas
// Solo existe en memoria del GroupManager, NO se serializa
// ----------------------------------------------------------------------------
class LFPG_FlagPositionCache
{
    vector m_Position;
    string m_GroupID;
    float m_RaiseProgress;
    int m_Tier;

    void LFPG_FlagPositionCache()
    {
        m_Position = vector.Zero;
        m_GroupID = "";
        m_RaiseProgress = 0.0;
        m_Tier = 1;
    }

    void Set(vector pos, string groupID, float progress, int tier)
    {
        m_Position = pos;
        m_GroupID = groupID;
        m_RaiseProgress = progress;
        m_Tier = tier;
    }
};

// ----------------------------------------------------------------------------
// LFPG_GroupsFileData - Wrapper para serializacion/deserializacion del JSON
// El JSON contiene un array de grupos
// ----------------------------------------------------------------------------
class LFPG_GroupsFileData
{
    int m_Version;
    ref array<ref LFPG_GroupData> m_Groups;

    void LFPG_GroupsFileData()
    {
        m_Version = 1;
        m_Groups = new array<ref LFPG_GroupData>;
    }
};
