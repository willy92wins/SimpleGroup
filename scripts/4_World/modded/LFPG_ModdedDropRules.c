// ============================================================================
// LFPG_ModdedDropRules.c - 4_World/modded
// Listed classnames cannot be dropped, crafted onto the ground, or moved
// onto the ground inside a foreign raised territory.
// A null manager or config does not block.
// ============================================================================

bool LFPG_IsListedDropBlocked(EntityAI item, PlayerBase player, vector pos)
{
    if (!item)
        return false;
    if (!player)
        return false;

    LFPG_GroupManager blockMgr = LFPG_GroupManager.Get();
    if (!blockMgr)
        return false;

    LFPG_TerritoryConfig blockCfg = blockMgr.GetConfig();
    if (!blockCfg)
        return false;
    if (!blockCfg.IsNoDropInForeignTerritory(item))
        return false;

    string blockGroup = "";
    PlayerIdentity blockIdentity = player.GetIdentity();
    if (blockIdentity)
        blockGroup = blockMgr.GetPlayerGroupID(blockIdentity.GetPlainId());

    string blockForeign = blockMgr.GetForeignOwnerAt(pos, blockGroup);
    if (blockForeign == "")
        return false;
    return true;
}

modded class ActionDropItem
{
    override void OnExecuteServer(ActionData action_data)
    {
        if (GetGame().IsDedicatedServer() && action_data && action_data.m_Player)
        {
            PlayerBase dropPlayer = action_data.m_Player;
            ItemBase heldItem = dropPlayer.GetItemInHands();
            if (heldItem && LFPG_IsListedDropBlocked(heldItem, dropPlayer, dropPlayer.GetPosition()))
            {
                PlayerIdentity dropIdentity = dropPlayer.GetIdentity();
                LFPG_GroupManager dropMgr = LFPG_GroupManager.Get();
                if (dropMgr && dropIdentity)
                    dropMgr.SendErrorToPlayer(dropIdentity, dropPlayer, "#STR_LFPG_ERR_DROP_RESTRICTED");

                string dropActLog = "ActionDropItem blocked ";
                dropActLog = dropActLog + heldItem.GetType();
                LFPG_Log.Info(dropActLog);
                return;
            }
        }

        super.OnExecuteServer(action_data);
    }
}

modded class CraftWoodenCrate
{
    override bool CanDo(ItemBase ingredients[], PlayerBase player)
    {
        if (!super.CanDo(ingredients, player))
            return false;

        if (!GetGame().IsDedicatedServer())
            return true;
        if (!player)
            return true;

        LFPG_GroupManager craftMgr = LFPG_GroupManager.Get();
        if (!craftMgr)
            return true;

        LFPG_TerritoryConfig craftCfg = craftMgr.GetConfig();
        if (!craftCfg)
            return true;

        string craftGroup = "";
        string craftUID = "";
        PlayerIdentity craftIdentity = player.GetIdentity();
        if (craftIdentity)
        {
            craftUID = craftIdentity.GetPlainId();
            craftGroup = craftMgr.GetPlayerGroupID(craftUID);
        }

        // Blacklist wins over every exemption. The result is not spawned on the
        // player: it is raycast DEFAULT_SPAWN_DISTANCE ahead and dispersed by
        // UAItemsSpreadRadius.DEFAULT, so the foreign check uses that reach.
        bool crateListed = false;
        int craftI;
        int craftCount;
        if (craftCfg.m_NoDropInForeignTerritoryTypes)
        {
            craftCount = craftCfg.m_NoDropInForeignTerritoryTypes.Count();
            for (craftI = 0; craftI < craftCount; craftI = craftI + 1)
            {
                string craftEntry = craftCfg.m_NoDropInForeignTerritoryTypes[craftI];
                if (GetGame().IsKindOf("WoodenCrate", craftEntry))
                    crateListed = true;
            }
        }
        if (crateListed)
        {
            float craftReach = DEFAULT_SPAWN_DISTANCE + UAItemsSpreadRadius.DEFAULT;
            string craftForeign = craftMgr.GetForeignOwnerAt(player.GetPosition(), craftGroup, craftReach);
            if (craftForeign != "")
                return false;
        }

        // Same cap as a drop, and only when the crate itself counts as furniture.
        bool crateCounts = false;
        if (craftCfg.m_FurnitureCountedTypes)
        {
            craftCount = craftCfg.m_FurnitureCountedTypes.Count();
            for (craftI = 0; craftI < craftCount; craftI = craftI + 1)
            {
                string countedEntry = craftCfg.m_FurnitureCountedTypes[craftI];
                if (GetGame().IsKindOf("WoodenCrate", countedEntry))
                    crateCounts = true;
            }
        }
        if (craftCfg.m_FurnitureExcludedTypes)
        {
            craftCount = craftCfg.m_FurnitureExcludedTypes.Count();
            for (craftI = 0; craftI < craftCount; craftI = craftI + 1)
            {
                string excludedEntry = craftCfg.m_FurnitureExcludedTypes[craftI];
                if (GetGame().IsKindOf("WoodenCrate", excludedEntry))
                    crateCounts = false;
            }
        }
        if (crateCounts && craftUID != "")
        {
            if (craftMgr.IsInBuildZone(craftUID, player.GetPosition()))
            {
                if (!craftMgr.CanDeploy(craftGroup))
                    return false;
            }
        }
        return true;
    }

    // The crate already exists. Recount the zone that contains it. Do not hot-count.
    override void Do(ItemBase ingredients[], PlayerBase player, array<ItemBase> results, float specialty_weight)
    {
        super.Do(ingredients, player, results, specialty_weight);

        if (!GetGame().IsDedicatedServer())
            return;
        if (!results)
            return;

        LFPG_GroupManager craftDoMgr = LFPG_GroupManager.Get();
        if (!craftDoMgr)
            return;

        int resultCount = results.Count();
        int resultI;
        for (resultI = 0; resultI < resultCount; resultI = resultI + 1)
        {
            ItemBase craftedItem = results[resultI];
            if (!craftedItem)
                continue;
            string craftedGroup = craftDoMgr.FindGroupIDAtPosition(craftedItem.GetPosition());
            if (craftedGroup != "")
                craftDoMgr.QueueRecalibrate(craftedGroup);
        }
    }
}

// DayZPlayerInventory is engine-owned and cannot be modded (DayZ 1.29).
// ActionDropItem, ItemBase.CanSwapEntities and the server-side deferred
// EEItemLocationChanged return enforce the blacklist using supported hooks.
