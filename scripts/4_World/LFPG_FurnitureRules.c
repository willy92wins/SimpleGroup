// ============================================================================
// LFPG_FurnitureRules.c - 4_World
// Single predicate for what occupies a furniture slot.
// Base-building parts and deployables count. Config adds further types.
// Kits, holograms and the exclusion lists do not.
// A packed tent does not count on a drop or a recount. Placement is the
// exception: TentBase.OnPlacementComplete calls super while still packed,
// then pitches. countPackedTent is for that path only.
// ============================================================================

bool LFPG_CountsAsFurniture(EntityAI ent, bool countPackedTent)
{
    if (!ent)
        return false;

    if (LFPG_IsExcludedFromDeploy(ent))
        return false;

    LFPG_GroupManager furnMgr = LFPG_GroupManager.Get();
    LFPG_TerritoryConfig furnCfg = null;
    if (furnMgr)
        furnCfg = furnMgr.GetConfig();

    if (furnCfg && furnCfg.IsTypeExcludedFromFurniture(ent))
        return false;

    if (ent.IsHologram())
        return false;

    ItemBase furnItem = ItemBase.Cast(ent);
    if (furnItem && furnItem.IsBasebuildingKit())
        return false;

    if (!countPackedTent)
    {
        TentBase furnTent = TentBase.Cast(ent);
        if (furnTent && furnTent.GetState() == TentBase.PACKED)
            return false;
    }

    if (ent.IsInherited(BaseBuildingBase))
        return true;

    if (furnCfg && furnCfg.IsCountedFurnitureType(ent))
        return true;

    if (furnItem && furnItem.IsDeployable())
        return true;

    return false;
}
