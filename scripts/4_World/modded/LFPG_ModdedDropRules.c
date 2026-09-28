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

                string dropActLog = "[SimpleGroup] ActionDropItem blocked ";
                dropActLog = dropActLog + heldItem.GetType();
                PrintToRPT(dropActLog);
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
        if (!craftCfg.m_NoDropInForeignTerritoryTypes)
            return true;

        bool crateListed = false;
        int craftCount = craftCfg.m_NoDropInForeignTerritoryTypes.Count();
        int craftI;
        for (craftI = 0; craftI < craftCount; craftI = craftI + 1)
        {
            string craftEntry = craftCfg.m_NoDropInForeignTerritoryTypes[craftI];
            if (GetGame().IsKindOf("WoodenCrate", craftEntry))
                crateListed = true;
        }
        if (!crateListed)
            return true;

        string craftGroup = "";
        PlayerIdentity craftIdentity = player.GetIdentity();
        if (craftIdentity)
            craftGroup = craftMgr.GetPlayerGroupID(craftIdentity.GetPlainId());

        string craftForeign = craftMgr.GetForeignOwnerAt(player.GetPosition(), craftGroup);
        if (craftForeign != "")
            return false;
        return true;
    }
}

modded class DayZPlayerInventory
{
    override bool PlayerCheckRequestDst(notnull InventoryLocation src, notnull InventoryLocation dst, float radius)
    {
        if (!super.PlayerCheckRequestDst(src, dst, radius))
            return false;

        if (!GetGame().IsDedicatedServer())
            return true;
        if (dst.GetType() != InventoryLocationType.GROUND)
            return true;

        EntityAI dstItem = dst.GetItem();
        if (!dstItem)
            return true;

        DayZPlayer dstOwner = GetDayZPlayerOwner();
        PlayerBase dstPlayer = PlayerBase.Cast(dstOwner);
        if (!dstPlayer)
            return true;

        if (LFPG_IsListedDropBlocked(dstItem, dstPlayer, dst.GetPos()))
            return false;
        return true;
    }
}
