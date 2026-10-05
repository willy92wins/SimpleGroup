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

// Decode a quoted JSON key with the engine, including escaped key names.
class LFPG_ConfigKeyToken
{
    string key;
};

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
        m_FurnitureExcludedTypes.Insert("Plastic_Explosive");
        m_FurnitureExcludedTypes.Insert("ImprovisedExplosive");
        m_FurnitureExcludedTypes.Insert("ClaymoreMine");

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
        // Sanity checks
        if (m_MaxGroupSize < 1)
            m_MaxGroupSize = 1;
        if (m_MaxGroupSize > 20)
            m_MaxGroupSize = 20;
        int previousBuildRadius = m_BuildRadiusMeters;
        int previousTerritoryRadius = m_TerritoryRadiusMeters;
        int previousInviteDuration = m_InviteDurationSeconds;
        // Keep integer products below int.MAX before computing derived values.
        if (m_BuildRadiusMeters < 5)
            m_BuildRadiusMeters = 5;
        if (m_BuildRadiusMeters > 10000)
            m_BuildRadiusMeters = 10000;
        if (m_TerritoryRadiusMeters < 50)
            m_TerritoryRadiusMeters = 50;
        if (m_TerritoryRadiusMeters > 10000)
            m_TerritoryRadiusMeters = 10000;
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
        if (m_InviteDurationSeconds > 3600)
            m_InviteDurationSeconds = 3600;
        if (previousBuildRadius != m_BuildRadiusMeters)
            LFPG_Log.Info("Config: m_BuildRadiusMeters clamped to " + m_BuildRadiusMeters.ToString());
        if (previousTerritoryRadius != m_TerritoryRadiusMeters)
            LFPG_Log.Info("Config: m_TerritoryRadiusMeters clamped to " + m_TerritoryRadiusMeters.ToString());
        if (previousInviteDuration != m_InviteDurationSeconds)
            LFPG_Log.Info("Config: m_InviteDurationSeconds clamped to " + m_InviteDurationSeconds.ToString());

        // Keep explicit tier list lengths; consumers supply missing entries.
        if (!m_TierDeployLimits)
            m_TierDeployLimits = new array<int>;
        int tdi;
        for (tdi = 0; tdi < m_TierDeployLimits.Count(); tdi = tdi + 1)
        {
            if (m_TierDeployLimits[tdi] < 0)
                m_TierDeployLimits[tdi] = 0;
        }

        if (!m_TierDurations)
            m_TierDurations = new array<int>;
        int tdu;
        for (tdu = 0; tdu < m_TierDurations.Count(); tdu = tdu + 1)
        {
            if (m_TierDurations[tdu] < 60)
                m_TierDurations[tdu] = 60;
        }

        if (!m_TierFlagActionsEnabled)
            m_TierFlagActionsEnabled = new array<int>;

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
        // Restore legacy timing defaults when no positive value was supplied.

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

        // Load restores omitted lists from the validated JSON key set.

        // The constructor supplies m_MinRefreshLifetime when the key is absent.
        // Keep an explicit admin value even when the file still declares v4 or older.
        // Load deliberately leaves existing config files unchanged on disk.

        m_ConfigVersion = LFPG_CONFIG_VERSION;
    }

    // Obtiene la duracion del tier en segundos
    int GetTierDuration(int tier)
    {
        int idx = tier - 1;
        if (idx < 0)
            idx = 0;
        if (!m_TierDurations || m_TierDurations.Count() == 0)
            return 172800;
        if (idx >= m_TierDurations.Count() && m_TierDurations.Count() < 3)
            return 172800;
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
        if (!m_TierFlagActionsEnabled || m_TierFlagActionsEnabled.Count() == 0)
            return true;
        if (idx >= m_TierFlagActionsEnabled.Count() && m_TierFlagActionsEnabled.Count() < 3)
            return true;
        if (idx >= m_TierFlagActionsEnabled.Count())
            idx = m_TierFlagActionsEnabled.Count() - 1;
        return (m_TierFlagActionsEnabled[idx] != 0);
    }

    // Chequea si un EntityAI esta en la greenhouse whitelist
    // PERF (issue #24, PR1): memoized by classname via LFPG_KindMemo.
    bool IsGreenhouse(EntityAI ent)
    {
        if (!m_EnableGreenhouseAsPlot || !m_EnablePlots)
            return false;

        return LFPG_KindMemo.MatchList(ent, "gh", m_GreenhouseWhitelist);
    }

    // Chequea si un EntityAI esta en la lista de exclusion configurable
    // PERF (issue #24, PR1): memoized by classname via LFPG_KindMemo.
    bool IsTypeExcludedFromFurniture(EntityAI ent)
    {
        return LFPG_KindMemo.MatchList(ent, "fexcl", m_FurnitureExcludedTypes);
    }

    // Lista A: placement sin requerir grupo/territorio propio (pero bloqueado en ajeno)
    // PERF (issue #24, PR1): memoized by classname via LFPG_KindMemo.
    bool IsNoBaseRequired(EntityAI ent)
    {
        return LFPG_KindMemo.MatchList(ent, "nbr", m_NoBaseRequiredTypes);
    }

    // Lista B: placement sin restriccion alguna (prioridad sobre lista A)
    // PERF (issue #24, PR1): memoized by classname via LFPG_KindMemo.
    bool IsUnrestricted(EntityAI ent)
    {
        return LFPG_KindMemo.MatchList(ent, "unr", m_UnrestrictedTypes);
    }

    // Extra furniture types (crates, chests, barrels) counted in addition to parts and deployables.
    // PERF (issue #24, PR1): memoized by classname via LFPG_KindMemo.
    bool IsCountedFurnitureType(EntityAI ent)
    {
        return LFPG_KindMemo.MatchList(ent, "cnt", m_FurnitureCountedTypes);
    }

    // Blacklist: cannot be dropped or placed in a foreign territory.
    // PERF (issue #24, PR1): memoized by classname via LFPG_KindMemo.
    bool IsNoDropInForeignTerritory(EntityAI ent)
    {
        return LFPG_KindMemo.MatchList(ent, "ndrp", m_NoDropInForeignTerritoryTypes);
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
    // JsonSerializer turns omitted arrays into allocated empty arrays, even
    // when the destination was null. Capture top-level key presence from the
    // same validated JSON snapshot so an explicit [] remains an admin choice.
    static bool FindListKeys(string raw, out bool hasNoBase, out bool hasCounted, out bool hasNoDrop)
    {
        hasNoBase = false;
        hasCounted = false;
        hasNoDrop = false;
        array<string> keys;
        if (!FindTopLevelKeys(raw, keys))
            return false;
        hasNoBase = keys.Find("m_NoBaseRequiredTypes") >= 0;
        hasCounted = keys.Find("m_FurnitureCountedTypes") >= 0;
        hasNoDrop = keys.Find("m_NoDropInForeignTerritoryTypes") >= 0;
        return true;
    }

    // Scan only engine-validated JSON; decode escaped names with the same parser.
    static bool FindTopLevelKeys(string raw, out array<string> keys)
    {
        keys = new array<string>;
        int depth = 0;
        bool hasRoot = false;
        bool inString = false;
        bool escaped = false;
        bool expectKey = false;
        int start = 0;
        int length = raw.Length();
        string block;
        int scanStart = 0;
        if (length >= 3)
        {
            int bomFirst = raw.Get(0).ToAscii();
            int bomSecond = raw.Get(1).ToAscii();
            int bomThird = raw.Get(2).ToAscii();
            if (bomFirst < 0)
                bomFirst = bomFirst + 256;
            if (bomSecond < 0)
                bomSecond = bomSecond + 256;
            if (bomThird < 0)
                bomThird = bomThird + 256;
            if (bomFirst == 239 && bomSecond == 187 && bomThird == 191)
                scanStart = 3;
        }
        for (int i = 0; i < length; i++)
        {
            int inBlock = i % 512;
            if (inBlock == 0)
            {
                int blockLength = length - i;
                if (blockLength > 512)
                    blockLength = 512;
                block = raw.Substring(i, blockLength);
                if (block.Length() != blockLength)
                    return false;
            }
            if (i < scanStart)
                continue;
            string character = block.Get(inBlock);
            if (inString)
            {
                if (escaped)
                    escaped = false;
                else if (character == "\\")
                    escaped = true;
                else if (character == "\"")
                {
                    inString = false;
                    if (depth == 1 && expectKey)
                    {
                        LFPG_ConfigKeyToken token = new LFPG_ConfigKeyToken();
                        string error;
                        string keyJSON = "{\"key\":" + raw.Substring(start, i - start + 1) + "}";
                        if (!JsonFileLoader<LFPG_ConfigKeyToken>.LoadData(keyJSON, token, error))
                            return false;
                        keys.Insert(token.key);
                        expectKey = false;
                    }
                }
                continue;
            }
            if (depth == 0 && character != " " && character != "\t" && character != "\r" && character != "\n")
            {
                if (hasRoot || character != "{")
                    return false;
                hasRoot = true;
            }
            if (character == "\"")
            {
                inString = true;
                start = i;
            }
            else if (character == "{" || character == "[")
            {
                depth++;
                if (depth == 1)
                    expectKey = true;
            }
            else if (character == "}" || character == "]")
                depth--;
            else if (character == "," && depth == 1)
                expectKey = true;
        }
        return hasRoot && depth == 0 && !inString;
    }

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
            string raw;
            if (!LFPG_GroupsStorage.ReadText(filePath, raw) || !JsonFileLoader<LFPG_TerritoryConfig>.LoadData(raw, config, loadError))
            {
                // Deserialization may have changed a prefix of the object.
                // Discard that partial state and preserve the admin's file.
                LFPG_Log.Error("Config load failed; using defaults for this session. Original file retained: " + loadError);
                config = new LFPG_TerritoryConfig();
                config.ComputeDerivedValues();
                return config;
            }

            // Omitted lists use constructor defaults in every config version.
            array<string> keys;
            if (FindTopLevelKeys(raw, keys))
            {
                LFPG_TerritoryConfig defaults = new LFPG_TerritoryConfig();
                if (keys.Find("m_FurnitureExcludedTypes") < 0)
                    config.m_FurnitureExcludedTypes = defaults.m_FurnitureExcludedTypes;
                if (keys.Find("m_NoBaseRequiredTypes") < 0)
                    config.m_NoBaseRequiredTypes = defaults.m_NoBaseRequiredTypes;
                if (keys.Find("m_FurnitureCountedTypes") < 0)
                    config.m_FurnitureCountedTypes = defaults.m_FurnitureCountedTypes;
                if (keys.Find("m_NoDropInForeignTerritoryTypes") < 0)
                    config.m_NoDropInForeignTerritoryTypes = defaults.m_NoDropInForeignTerritoryTypes;
                if (keys.Find("m_TierDeployLimits") < 0)
                    config.m_TierDeployLimits = defaults.m_TierDeployLimits;
                if (keys.Find("m_TierDurations") < 0)
                    config.m_TierDurations = defaults.m_TierDurations;
                if (keys.Find("m_TierFlagActionsEnabled") < 0)
                    config.m_TierFlagActionsEnabled = defaults.m_TierFlagActionsEnabled;
            }
            else
            {
                // Keep parsed values if the scanner and engine disagree.
                LFPG_Log.Error("Cannot inspect config keys; keeping the parsed admin values.");
            }
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
