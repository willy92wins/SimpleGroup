// ============================================================================
// LFPG_TerritoryConfig.c - 3_Game
// Configuracion del servidor - serializable a/desde JSON
//
// Archivo: $profile/SimpleGroup/config.json
// Se carga una vez al inicio del servidor.
// Si no existe, se crea con valores por defecto.
// ============================================================================

class LFPG_TerritoryConfig
{
    // --- Grupo ---
    int m_MaxGroupSize;

    // --- Radios ---
    int m_BuildRadiusMeters;
    int m_TerritoryRadiusMeters;

    // --- Invitacion ---
    int m_InviteDurationSeconds;

    // --- Nombre de grupo ---
    int m_GroupNameMinLength;
    int m_GroupNameMaxLength;

    // --- Tiers ---
    ref array<int> m_TierDeployLimits;
    ref array<int> m_TierDurations;
    int m_MaxGardenPlotsPerFlag;

    // --- Bandera ---
    float m_FlagRaiseRatePerSecond;
    float m_FlagLowerRatePerSecond;

    // --- Comportamiento ---
    bool m_DestroyDeployedOnDissolve;

    // --- Pre-computed (runtime only, NO serializado) ---
    [NonSerialized()]
    float m_BuildRadiusSq;
    [NonSerialized()]
    float m_TerritoryRadiusSq;

    void LFPG_TerritoryConfig()
    {
        // Valores por defecto
        m_MaxGroupSize = 6;
        m_BuildRadiusMeters = 30;
        m_TerritoryRadiusMeters = 500;
        m_InviteDurationSeconds = 10;
        m_GroupNameMinLength = 3;
        m_GroupNameMaxLength = 24;

        m_TierDeployLimits = new array<int>;
        m_TierDeployLimits.Insert(8);
        m_TierDeployLimits.Insert(12);
        m_TierDeployLimits.Insert(16);

        // Duracion en segundos: T1=2d, T2=5d, T3=7d
        m_TierDurations = new array<int>;
        m_TierDurations.Insert(172800);
        m_TierDurations.Insert(432000);
        m_TierDurations.Insert(604800);

        m_MaxGardenPlotsPerFlag = 3;
        m_FlagRaiseRatePerSecond = 0.01;
        m_FlagLowerRatePerSecond = 0.002;
        m_DestroyDeployedOnDissolve = false;

        m_BuildRadiusSq = 0;
        m_TerritoryRadiusSq = 0;
    }

    // Calcula valores derivados tras cargar del JSON
    void ComputeDerivedValues()
    {
        m_BuildRadiusSq = m_BuildRadiusMeters * m_BuildRadiusMeters;
        m_TerritoryRadiusSq = m_TerritoryRadiusMeters * m_TerritoryRadiusMeters;

        // Sanity checks
        if (m_MaxGroupSize < 1)
            m_MaxGroupSize = 1;
        if (m_MaxGroupSize > 20)
            m_MaxGroupSize = 20;
        if (m_BuildRadiusMeters < 5)
            m_BuildRadiusMeters = 5;
        if (m_TerritoryRadiusMeters < 50)
            m_TerritoryRadiusMeters = 50;
        if (m_GroupNameMinLength < 1)
            m_GroupNameMinLength = 1;
        if (m_GroupNameMaxLength > 48)
            m_GroupNameMaxLength = 48;
        if (m_InviteDurationSeconds < 5)
            m_InviteDurationSeconds = 5;

        // Asegurar que TierDeployLimits tiene 3 entries
        while (m_TierDeployLimits.Count() < 3)
        {
            m_TierDeployLimits.Insert(8);
        }

        // Asegurar que TierDurations tiene 3 entries
        while (m_TierDurations.Count() < 3)
        {
            m_TierDurations.Insert(172800);
        }

        // Recalcular con valores posiblemente corregidos
        m_BuildRadiusSq = m_BuildRadiusMeters * m_BuildRadiusMeters;
        m_TerritoryRadiusSq = m_TerritoryRadiusMeters * m_TerritoryRadiusMeters;
    }

    // Obtiene la duracion del tier en segundos
    int GetTierDuration(int tier)
    {
        int idx = tier - 1;
        if (idx < 0)
            idx = 0;
        if (idx >= m_TierDurations.Count())
            idx = m_TierDurations.Count() - 1;
        return m_TierDurations[idx];
    }

    // ========================================================================
    // Carga/Guarda - Rutas de archivo
    // ========================================================================

    static string GetConfigDir()
    {
        return "$profile:SimpleGroup";
    }

    static string GetConfigPath()
    {
        return "$profile:SimpleGroup/config.json";
    }

    static string GetGroupsPath()
    {
        return "$profile:SimpleGroup/groups.json";
    }

    static string GetGroupsBackupPath()
    {
        return "$profile:SimpleGroup/groups.json.bak";
    }

    static string GetGroupsTmpPath()
    {
        return "$profile:SimpleGroup/groups.json.tmp";
    }

    // Carga config desde JSON. Si no existe, crea con defaults y guarda.
    static LFPG_TerritoryConfig Load()
    {
        LFPG_TerritoryConfig config;
        string dirPath = GetConfigDir();
        string filePath = GetConfigPath();

        // Crear directorio si no existe
        if (!FileExist(dirPath))
        {
            MakeDirectory(dirPath);
        }

        if (FileExist(filePath))
        {
            config = new LFPG_TerritoryConfig();
            JsonFileLoader<LFPG_TerritoryConfig>.JsonLoadFile(filePath, config);
            string loadMsg = "[SimpleGroup] Config loaded from: ";
            loadMsg = loadMsg + filePath;
            Print(loadMsg);
        }
        else
        {
            config = new LFPG_TerritoryConfig();
            JsonFileLoader<LFPG_TerritoryConfig>.JsonSaveFile(filePath, config);
            string createMsg = "[SimpleGroup] Default config created at: ";
            createMsg = createMsg + filePath;
            Print(createMsg);
        }

        config.ComputeDerivedValues();
        return config;
    }
};
