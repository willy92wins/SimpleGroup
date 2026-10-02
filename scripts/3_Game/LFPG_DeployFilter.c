// ============================================================================
// LFPG_DeployFilter.c - 3_Game
// Filtro de exclusion: determina si un EntityAI NUNCA debe contar como mueble
//
// En 3_Game para que cargue antes que todo 4_World.
// NO usa ItemBase (no disponible en 3_Game).
// El check de IsDeployable() se hace inline en 4_World donde ItemBase existe.
// ============================================================================

static bool LFPG_IsExcludedFromDeploy(EntityAI item)
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

// Furniture exemptions also exempt placement from group, zone and quota rules.
// Flags and plots use dedicated placement rules, not the furniture quota.
static bool LFPG_IsFurniturePlacementExempt(EntityAI item, array<string> excludedTypes)
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
