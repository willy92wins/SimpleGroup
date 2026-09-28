// ============================================================================
// LFPG_ToolMatcher.c - 3_Game
// Helpers centralizados para detectar herramientas usadas en acciones LFPG.
//
// FIX I-8, M-14: Centraliza la logica de IsKindOf para sledge/pickaxe/hatchet
// para que mods puedan anadir variantes custom (ej. herencia de SledgeHammer o
// Pickaxe no cubrira Mallet si no hereda).
//
// La extensibilidad es opcional via cfg (no implementada aqui - requeriria
// anadir campos al TerritoryConfig). Por ahora todos los checks son IsKindOf
// de clases vanilla + algunos variants conocidos.
// ============================================================================

static bool LFPG_IsSledgeHammer(EntityAI ent)
{
    if (!ent)
        return false;
    string kSledge = "SledgeHammer";
    if (ent.IsKindOf(kSledge))
        return true;
    // Variantes comunes de mods
    string kMallet = "Mallet";
    if (ent.IsKindOf(kMallet))
        return true;
    return false;
}

static bool LFPG_IsPickaxe(EntityAI ent)
{
    if (!ent)
        return false;
    string kPick = "Pickaxe";
    if (ent.IsKindOf(kPick))
        return true;
    return false;
}

static bool LFPG_IsHatchet(EntityAI ent)
{
    if (!ent)
        return false;
    string kHatchet = "Hatchet";
    if (ent.IsKindOf(kHatchet))
        return true;
    // DayZ vanilla tiene variantes: FirefighterAxe, WoodAxe etc. No heredan de Hatchet
    // pero si de Axe_Base. Aceptar Axe_Base como hatchet universal para destruir bandera.
    string kAxeBase = "Axe_Base";
    if (ent.IsKindOf(kAxeBase))
        return true;
    return false;
}
