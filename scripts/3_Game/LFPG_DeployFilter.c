// ============================================================================
// LFPG_DeployFilter.c - 3_Game
// Filtro de exclusion: determina si un EntityAI NUNCA debe contar como mueble
//
// En 3_Game para que cargue antes que todo 4_World.
// NO usa ItemBase (no disponible en 3_Game).
// El check de IsDeployable() se hace inline en 4_World donde ItemBase existe.
// ============================================================================

// PERF (issue #24, PR1): per-classname memo for IsKindOf walks.
// Every config list below is loaded once in Init and never mutates at runtime,
// so predicate+typename memoization is behavior-preserving. It turns the
// ~25-30 IsKindOf per inventory move (EEItemLocationChanged hot path) into one
// walk per classname ever. Clear on Init / placement-rules sync as insurance.
class LFPG_KindMemo
{
    protected static ref map<string, bool> s_Memo;

    static void Clear()
    {
        if (s_Memo)
            s_Memo.Clear();
    }

    static string Key(string pred, string typeName)
    {
        return pred + "|" + typeName;
    }

    static bool Find(string pred, string typeName, out bool value)
    {
        value = false;
        if (!s_Memo)
            return false;
        return s_Memo.Find(Key(pred, typeName), value);
    }

    static void Store(string pred, string typeName, bool value)
    {
        if (!s_Memo)
            s_Memo = new map<string, bool>;
        s_Memo.Set(Key(pred, typeName), value);
    }

    static bool MatchList(EntityAI ent, string pred, array<string> types)
    {
        if (!ent)
            return false;
        string typeName = ent.GetType();
        bool cached;
        if (Find(pred, typeName, cached))
            return cached;
        bool v = false;
        if (types)
        {
            for (int i = 0; i < types.Count(); i = i + 1)
            {
                if (types[i] != "" && ent.IsKindOf(types[i]))
                {
                    v = true;
                    break;
                }
            }
        }
        Store(pred, typeName, v);
        return v;
    }
};

static bool LFPG_IsExcludedFromDeployUncached(EntityAI item)
{
    if (!item)
        return true;

    // Items LFPG propios (banderas y kits)
    string flagBaseType = "LFPG_FlagBase";
    if (item.IsKindOf(flagBaseType))
        return true;

    string flagKitType = "LFPG_FlagKit_T1";
    if (item.IsKindOf(flagKitType))
        return true;

    // Garden plots tienen su propio counter separado
    string gardenType = "GardenPlot";
    if (item.IsKindOf(gardenType))
        return true;

    // Ropa (backpacks, vests, pouches, gorros, botas, guantes, etc.)
    string clothingType = "Clothing_Base";
    if (item.IsKindOf(clothingType))
        return true;

    // Armas y magazines
    string weaponType = "Weapon_Base";
    if (item.IsKindOf(weaponType))
        return true;

    string magType = "Magazine_Base";
    if (item.IsKindOf(magType))
        return true;

    return false;
}

// Memoized entry point: the 6 roots above never change at runtime.
static bool LFPG_IsExcludedFromDeploy(EntityAI item)
{
    if (!item)
        return true;
    string typeName = item.GetType();
    bool cached;
    if (LFPG_KindMemo.Find("excl", typeName, cached))
        return cached;
    bool v = LFPG_IsExcludedFromDeployUncached(item);
    LFPG_KindMemo.Store("excl", typeName, v);
    return v;
}

// Furniture exemptions also exempt placement from group, zone and quota rules.
// Flags and plots use dedicated placement rules, not the furniture quota.
// All callers pass the config furniture-exclusion list (server) or its synced
// mirror (client), both immutable within a session: memo by typename is valid.
static bool LFPG_IsFurniturePlacementExemptUncached(EntityAI item, array<string> excludedTypes)
{
    if (!item)
        return false;
    if (item.IsKindOf("LFPG_FlagBase") || item.IsKindOf("LFPG_FlagKit_T1") || item.IsKindOf("GardenPlot"))
        return false;
    if (LFPG_IsExcludedFromDeploy(item))
        return true;
    if (!excludedTypes)
        return false;
    for (int i = 0; i < excludedTypes.Count(); i = i + 1)
    {
        if (excludedTypes[i] != "" && item.IsKindOf(excludedTypes[i]))
            return true;
    }
    return false;
}

static bool LFPG_IsFurniturePlacementExempt(EntityAI item, array<string> excludedTypes)
{
    if (!item)
        return false;
    string typeName = item.GetType();
    bool cached;
    if (LFPG_KindMemo.Find("fpex", typeName, cached))
        return cached;
    bool v = LFPG_IsFurniturePlacementExemptUncached(item, excludedTypes);
    LFPG_KindMemo.Store("fpex", typeName, v);
    return v;
}
