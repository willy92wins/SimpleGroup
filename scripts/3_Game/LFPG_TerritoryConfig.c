// ============================================================================
// LFPG_TerritoryConfig.c - 3_Game
// Configuracion del servidor - serializable a/desde JSON
//
// Archivo: $profile/SimpleGroup/config.json
// Se carga una vez al inicio del servidor.
// Si no existe, se crea con valores por defecto.
//
// FIX I-1, I-2, I-13: m_FurnitureExcludedTypes default ampliado con tipos
//   conocidos de mods populares (Expansion, Traders, Fireplace, Fortifications)
// FIX G-3: m_RecalibrationIntervalSeconds default subido a 1800s (30min)
// FIX M-2: Magic numbers movidos a config (m_RpcThrottleMs, etc.)
// FIX M-21: m_ConfigVersion para merge de defaults nuevos al actualizar el mod
// FIX M-22: Sanity check de valores negativos
// ============================================================================

// FIX M-21: version actual del config. Si el config loaded tiene version < esta,
// se mergen defaults de los campos nuevos sin sobrescribir los existentes.
// v2 -> v3: anadidos m_NoBaseRequiredTypes y m_UnrestrictedTypes
// v3 -> v4: m_FurnitureCountedTypes and m_NoDropInForeignTerritoryTypes
// v4 -> v5: m_MinRefreshLifetime (seconds). Negative disables base refresh.
const int LFPG_CONFIG_VERSION = 5;

class LFPG_TerritoryConfig
{
    // --- Versionado ---
    int m_ConfigVersion;

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

    // --- Acciones de bandera por tier (1=habilitado, 0=deshabilitado) ---
    // Admin puede poner [1,1,0] para impedir subir/bajar manual en T3 (solo control via bateria)
    ref array<int> m_TierFlagActionsEnabled;

    // --- Comportamiento ---
    bool m_DestroyDeployedOnDissolve;

    // FIX E: Restriccion de drops de contenedores deployables
    bool m_EnforceContainerDropRestrictions;

    // --- Whitelist: tipos excluidos del conteo de muebles ---
    // Items deployables vanilla/mods que NO deben contar (BatteryCharger, Fireplace,
    // ExpansionMarket, Traders, etc.). Usa IsKindOf para cubrir herencia.
    // Tambien exentos de grupo/zona/cupo al colocar; la blacklist sigue prevaleciendo.
    ref array<string> m_FurnitureExcludedTypes;

    // --- Garden Plots ---
    bool m_EnablePlots;

    // --- Greenhouses como Plot ---
    bool m_EnableGreenhouseAsPlot;
    ref array<string> m_GreenhouseWhitelist;

    // --- Whitelist: placement con reglas especiales (v3+) ---
    // Lista A: items que se pueden colocar aunque el jugador NO tenga grupo/territorio,
    //   pero SIGUEN bloqueados si caen dentro de territorio ajeno. Ejemplo: el propio
    //   LFPG_FlagKit_T1 (creas tu primera bandera sin tener todavia grupo).
    // Lista B: items sin restriccion alguna — colocables en cualquier sitio incluyendo
    //   territorio ajeno, y NO cuentan al limite de deploys del tier.
    // Ambas listas chequean con IsKindOf para cubrir herencia.
    ref array<string> m_NoBaseRequiredTypes;
    ref array<string> m_UnrestrictedTypes;

    // Types that count as furniture in addition to base-building parts and deployables.
    ref array<string> m_FurnitureCountedTypes;

    // Classnames that cannot be dropped or placed inside a foreign territory.
    ref array<string> m_NoDropInForeignTerritoryTypes;

    // --- Recalibracion ---
    // FIX G-3: Interval largo por default (integrity check). Recalibrate es on-demand.
    int m_RecalibrationIntervalSeconds;

    // While a flag is raised, objects inside the build radius whose max lifetime
    // is at least this many seconds are reset to that max. Negative disables it.
    int m_MinRefreshLifetime;

    // --- Energia T3 ---
    float m_BatteryDrainPerSecond;
    float m_PowerGridConsumption;

    // --- Timings ajustables (FIX M-2) ---
    int m_RpcThrottleMs;
    int m_PlayerSyncDelayMs;
    int m_ValidationTickSeconds;

    // --- Pre-computed (runtime only, NO serializado) ---
    [NonSerialized()]
    float m_BuildRadiusSq;
    [NonSerialized()]
    float m_TerritoryRadiusSq;

    void LFPG_TerritoryConfig()
    {
        m_ConfigVersion = LFPG_CONFIG_VERSION;

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
        m_FlagRaiseRatePerSecond = 0.15;
        m_FlagLowerRatePerSecond = 0.08;

        // Por defecto: acciones de subir/bajar habilitadas para todos los tiers (1=on, 0=off)
        // Admin puede poner [1,1,0] para que T3 no permita subir/bajar manual
        m_TierFlagActionsEnabled = new array<int>;
        m_TierFlagActionsEnabled.Insert(1);
        m_TierFlagActionsEnabled.Insert(1);
        m_TierFlagActionsEnabled.Insert(1);

        m_DestroyDeployedOnDissolve = false;
        m_EnforceContainerDropRestrictions = true;

        // FIX I-1, I-2: Whitelist ampliada de tipos NO-mueble.
        // Cubre: BatteryCharger, Fireplace (y variantes), Traps (mines, bear, tripwire),
        // ExpansionMarket + ExpansionFlag (territory mod), Traders (NPCs/stalls),
        // Vehiculos (Transport/CarScript) para evitar falsos positivos con coches atados.
        m_FurnitureExcludedTypes = new array<string>;
        string e1 = "BatteryCharger";        m_FurnitureExcludedTypes.Insert(e1);
        string e2 = "FireplaceBase";         m_FurnitureExcludedTypes.Insert(e2);
        string e3 = "FireplaceIndoor";       m_FurnitureExcludedTypes.Insert(e3);
        string e4 = "Fireplace";             m_FurnitureExcludedTypes.Insert(e4);
        string e5 = "TrapBase";              m_FurnitureExcludedTypes.Insert(e5);
        string e6 = "BearTrap";              m_FurnitureExcludedTypes.Insert(e6);
        string e7 = "LandMineTrap";          m_FurnitureExcludedTypes.Insert(e7);
        string e8 = "TripwireTrap";          m_FurnitureExcludedTypes.Insert(e8);
        string e9 = "ExpansionMarketBoxContainer"; m_FurnitureExcludedTypes.Insert(e9);
        string e10 = "ExpansionFlag";        m_FurnitureExcludedTypes.Insert(e10);
        string e11 = "Trader_Base";          m_FurnitureExcludedTypes.Insert(e11);
        string e12 = "TraderBase";           m_FurnitureExcludedTypes.Insert(e12);
        string e13 = "CarScript";            m_FurnitureExcludedTypes.Insert(e13);
        string e14 = "Transport";            m_FurnitureExcludedTypes.Insert(e14);

        // FIX G-3: Recalibrate default 30min (antes 30s). Es fallback integrity check;
        // el flujo normal es on-demand por grupo.
        m_RecalibrationIntervalSeconds = 1800;
        m_MinRefreshLifetime = 86400;

        // Garden plots: conteo separado por defecto
        m_EnablePlots = true;

        // Greenhouses: desactivado por defecto
        m_EnableGreenhouseAsPlot = false;
        m_GreenhouseWhitelist = new array<string>;

        // Lista A — items placeables sin grupo/territorio propio, pero NO en ajeno.
        // Default: nuestro flag kit (crea la primera bandera del jugador).
        m_NoBaseRequiredTypes = new array<string>;
        string nbr1 = "LFPG_FlagKit_T1"; m_NoBaseRequiredTypes.Insert(nbr1);

        // Lista B — items sin restriccion alguna. Vacia por defecto; el admin
        // rellena segun necesidad del servidor.
        m_UnrestrictedTypes = new array<string>;

        m_FurnitureCountedTypes = new array<string>;
        string fc1 = "WoodenCrate"; m_FurnitureCountedTypes.Insert(fc1);
        string fc2 = "SeaChest"; m_FurnitureCountedTypes.Insert(fc2);
        string fc3 = "Barrel_ColorBase"; m_FurnitureCountedTypes.Insert(fc3);

        m_NoDropInForeignTerritoryTypes = new array<string>;
        string nd1 = "WoodenCrate"; m_NoDropInForeignTerritoryTypes.Insert(nd1);

        m_BatteryDrainPerSecond = 0.01;
        m_PowerGridConsumption = 10.0;

        // FIX M-2: Magic numbers movidos a config
        m_RpcThrottleMs = 500;
        m_PlayerSyncDelayMs = 2000;
        m_ValidationTickSeconds = 60;

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
        // AUDIT #10 F20: min <= max, ambas en [1, 48]. Sin esto un max < min o
        // un max <= 0 dejaba sin ningun nombre valido posible.
        if (m_GroupNameMinLength > 48)
            m_GroupNameMinLength = 48;
        if (m_GroupNameMaxLength < m_GroupNameMinLength)
            m_GroupNameMaxLength = m_GroupNameMinLength;
        if (m_InviteDurationSeconds < 5)
            m_InviteDurationSeconds = 5;

        // Asegurar que TierDeployLimits tiene 3 entries con sanity (FIX M-22)
        if (!m_TierDeployLimits)
            m_TierDeployLimits = new array<int>;
        while (m_TierDeployLimits.Count() < 3)
        {
            m_TierDeployLimits.Insert(8);
        }
        int tdi;
        for (tdi = 0; tdi < m_TierDeployLimits.Count(); tdi = tdi + 1)
        {
            if (m_TierDeployLimits[tdi] < 0)
                m_TierDeployLimits[tdi] = 0;
        }

        // Asegurar que TierDurations tiene 3 entries
        if (!m_TierDurations)
            m_TierDurations = new array<int>;
        while (m_TierDurations.Count() < 3)
        {
            m_TierDurations.Insert(172800);
        }
        int tdu;
        for (tdu = 0; tdu < m_TierDurations.Count(); tdu = tdu + 1)
        {
            if (m_TierDurations[tdu] < 60)
                m_TierDurations[tdu] = 60;
        }

        // Asegurar que TierFlagActionsEnabled tiene 3 entries
        if (!m_TierFlagActionsEnabled)
            m_TierFlagActionsEnabled = new array<int>;
        while (m_TierFlagActionsEnabled.Count() < 3)
        {
            m_TierFlagActionsEnabled.Insert(1);
        }

        // Energia T3
        if (m_BatteryDrainPerSecond < 0.001)
            m_BatteryDrainPerSecond = 0.001;
        if (m_PowerGridConsumption < 1.0)
            m_PowerGridConsumption = 1.0;

        // Recalibracion: clamp 60-3600s (default 1800)
        if (m_RecalibrationIntervalSeconds < 60)
            m_RecalibrationIntervalSeconds = 60;
        if (m_RecalibrationIntervalSeconds > 3600)
            m_RecalibrationIntervalSeconds = 3600;

        // Timings configurables
        if (m_RpcThrottleMs < 100)
            m_RpcThrottleMs = 100;
        if (m_RpcThrottleMs > 10000)
            m_RpcThrottleMs = 10000;
        if (m_PlayerSyncDelayMs < 500)
            m_PlayerSyncDelayMs = 500;
        if (m_PlayerSyncDelayMs > 30000)
            m_PlayerSyncDelayMs = 30000;
        if (m_ValidationTickSeconds < 10)
            m_ValidationTickSeconds = 10;
        if (m_ValidationTickSeconds > 600)
            m_ValidationTickSeconds = 600;

        // Asegurar que FurnitureExcludedTypes existe
        if (!m_FurnitureExcludedTypes)
        {
            m_FurnitureExcludedTypes = new array<string>;
        }

        // Asegurar que GreenhouseWhitelist existe
        if (!m_GreenhouseWhitelist)
        {
            m_GreenhouseWhitelist = new array<string>;
        }

        // Asegurar que las nuevas whitelists existen (v3+)
        if (!m_NoBaseRequiredTypes)
        {
            m_NoBaseRequiredTypes = new array<string>;
        }
        if (!m_UnrestrictedTypes)
        {
            m_UnrestrictedTypes = new array<string>;
        }
        if (!m_FurnitureCountedTypes)
        {
            m_FurnitureCountedTypes = new array<string>;
        }
        if (!m_NoDropInForeignTerritoryTypes)
        {
            m_NoDropInForeignTerritoryTypes = new array<string>;
        }

        // Recalcular con valores posiblemente corregidos
        m_BuildRadiusSq = m_BuildRadiusMeters * m_BuildRadiusMeters;
        m_TerritoryRadiusSq = m_TerritoryRadiusMeters * m_TerritoryRadiusMeters;
    }

    // FIX M-21: Mergear defaults de campos nuevos sin sobrescribir los existentes
    // del disco. Se invoca tras carga si m_ConfigVersion < LFPG_CONFIG_VERSION.
    void MergeNewDefaults()
    {
        // Si el config antiguo no tenia estos campos, los arrays seran null/vacios
        // y los campos numericos seran 0. Detectar 0 y asignar defaults.

        if (m_RpcThrottleMs <= 0)
            m_RpcThrottleMs = 500;
        if (m_PlayerSyncDelayMs <= 0)
            m_PlayerSyncDelayMs = 2000;
        if (m_ValidationTickSeconds <= 0)
            m_ValidationTickSeconds = 60;

        // The one-entry reset is the pre-v2 legacy list ("BatteryCharger" only).
        // v2 and newer keep the admin list, including an empty list or a single entry.
        // m_ConfigVersion is still the value loaded from disk here.
        if (m_ConfigVersion < 2)
        {
            if (!m_FurnitureExcludedTypes || m_FurnitureExcludedTypes.Count() <= 1)
            {
                LFPG_TerritoryConfig tmp = new LFPG_TerritoryConfig();
                m_FurnitureExcludedTypes = tmp.m_FurnitureExcludedTypes;
            }
        }

        // v3: si los nuevos arrays no existen en el config del disco, crear con defaults.
        // Si existen (admin los creo/edito manualmente) respetar su contenido.
        if (!m_NoBaseRequiredTypes)
        {
            LFPG_TerritoryConfig tmpA = new LFPG_TerritoryConfig();
            m_NoBaseRequiredTypes = tmpA.m_NoBaseRequiredTypes;
        }
        if (!m_UnrestrictedTypes)
        {
            m_UnrestrictedTypes = new array<string>;
        }

        // v4: create each new list with its defaults only when the loaded value is null.
        // An admin list is kept as stored, including an empty list.
        if (!m_FurnitureCountedTypes)
        {
            LFPG_TerritoryConfig tmpFurn = new LFPG_TerritoryConfig();
            m_FurnitureCountedTypes = tmpFurn.m_FurnitureCountedTypes;
        }
        if (!m_NoDropInForeignTerritoryTypes)
        {
            LFPG_TerritoryConfig tmpNoDrop = new LFPG_TerritoryConfig();
            m_NoDropInForeignTerritoryTypes = tmpNoDrop.m_NoDropInForeignTerritoryTypes;
        }

        // v5: one new scalar. Older files do not carry it; do not rewrite other fields.
        // A stored negative value on v5 means "disabled" and is left alone (Load skips
        // this method once the file is already v5).
        if (m_ConfigVersion < 5)
        {
            m_MinRefreshLifetime = 86400;
        }

        m_ConfigVersion = LFPG_CONFIG_VERSION;
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

    // Retorna si las acciones de subir/bajar bandera estan habilitadas para el tier
    bool IsFlagActionEnabled(int tier)
    {
        int idx = tier - 1;
        if (idx < 0)
            idx = 0;
        if (idx >= m_TierFlagActionsEnabled.Count())
            idx = m_TierFlagActionsEnabled.Count() - 1;
        return (m_TierFlagActionsEnabled[idx] != 0);
    }

    // Chequea si un EntityAI esta en la greenhouse whitelist
    bool IsGreenhouse(EntityAI ent)
    {
        if (!m_EnableGreenhouseAsPlot || !m_EnablePlots)
            return false;

        if (!m_GreenhouseWhitelist || !ent)
            return false;

        int count = m_GreenhouseWhitelist.Count();
        int i;
        for (i = 0; i < count; i = i + 1)
        {
            string ghType = m_GreenhouseWhitelist[i];
            if (ent.IsKindOf(ghType))
                return true;
        }
        return false;
    }

    // Chequea si un EntityAI esta en la lista de exclusion configurable
    bool IsTypeExcludedFromFurniture(EntityAI ent)
    {
        if (!m_FurnitureExcludedTypes || !ent)
            return false;

        int count = m_FurnitureExcludedTypes.Count();
        int i;
        for (i = 0; i < count; i = i + 1)
        {
            string excluded = m_FurnitureExcludedTypes[i];
            if (ent.IsKindOf(excluded))
                return true;
        }
        return false;
    }

    // Lista A: placement sin requerir grupo/territorio propio (pero bloqueado en ajeno)
    bool IsNoBaseRequired(EntityAI ent)
    {
        if (!m_NoBaseRequiredTypes || !ent)
            return false;

        int countNBR = m_NoBaseRequiredTypes.Count();
        int iNBR;
        for (iNBR = 0; iNBR < countNBR; iNBR = iNBR + 1)
        {
            string nbrType = m_NoBaseRequiredTypes[iNBR];
            if (ent.IsKindOf(nbrType))
                return true;
        }
        return false;
    }

    // Lista B: placement sin restriccion alguna (prioridad sobre lista A)
    bool IsUnrestricted(EntityAI ent)
    {
        if (!m_UnrestrictedTypes || !ent)
            return false;

        int countU = m_UnrestrictedTypes.Count();
        int iU;
        for (iU = 0; iU < countU; iU = iU + 1)
        {
            string uType = m_UnrestrictedTypes[iU];
            if (ent.IsKindOf(uType))
                return true;
        }
        return false;
    }

    // Extra furniture types (crates, chests, barrels) counted in addition to parts and deployables.
    bool IsCountedFurnitureType(EntityAI ent)
    {
        if (!m_FurnitureCountedTypes || !ent)
            return false;

        int countFC = m_FurnitureCountedTypes.Count();
        int iFC;
        for (iFC = 0; iFC < countFC; iFC = iFC + 1)
        {
            string fcType = m_FurnitureCountedTypes[iFC];
            if (ent.IsKindOf(fcType))
                return true;
        }
        return false;
    }

    // Blacklist: cannot be dropped or placed in a foreign territory.
    bool IsNoDropInForeignTerritory(EntityAI ent)
    {
        if (!m_NoDropInForeignTerritoryTypes || !ent)
            return false;

        int countND = m_NoDropInForeignTerritoryTypes.Count();
        int iND;
        for (iND = 0; iND < countND; iND = iND + 1)
        {
            string ndType = m_NoDropInForeignTerritoryTypes[iND];
            if (ent.IsKindOf(ndType))
                return true;
        }
        return false;
    }

    // ========================================================================
    // Carga/Guarda
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
    // FIX M-21: Mergea defaults nuevos si version cargada < actual.
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
            string loadError = "";
            if (!JsonFileLoader<LFPG_TerritoryConfig>.LoadFile(filePath, config, loadError))
            {
                // Deserialization may have changed a prefix of the object.
                // Discard that partial state and preserve the admin's file.
                LFPG_Log.Error("Config load failed; using defaults for this session. Original file retained: " + loadError);
                config = new LFPG_TerritoryConfig();
                config.ComputeDerivedValues();
                return config;
            }

            // FIX M-21: Mergear defaults si el config es de version previa
            if (config.m_ConfigVersion < LFPG_CONFIG_VERSION)
            {
                string migMsg = "Config version outdated (";
                migMsg = migMsg + config.m_ConfigVersion.ToString();
                migMsg = migMsg + " -> ";
                migMsg = migMsg + LFPG_CONFIG_VERSION.ToString();
                migMsg = migMsg + "), merging new defaults.";
                LFPG_Log.Info(migMsg);
                config.MergeNewDefaults();
                // Apply new defaults in memory. Never truncate an existing
                // admin config during startup just to persist a version bump.
            }

            string loadMsg = "Config loaded from: ";
            loadMsg = loadMsg + filePath;
            LFPG_Log.Info(loadMsg);
        }
        else
        {
            config = new LFPG_TerritoryConfig();
            string saveError = "";
            if (!JsonFileLoader<LFPG_TerritoryConfig>.SaveFile(filePath, config, saveError))
                LFPG_Log.Error("Cannot create default config: " + saveError);
            else
            {
                string createMsg = "Default config created at: ";
                createMsg = createMsg + filePath;
                LFPG_Log.Info(createMsg);
            }
        }

        config.ComputeDerivedValues();
        return config;
    }
};
