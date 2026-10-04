// Server-side distance for flag actions. CCTCursor trusts the client cursor hit.
class LFPG_ActionGuards
{
    static const float FLAG_ACTION_MAX_DISTANCE = 5.0;

    static bool IsPlayerNearTarget(PlayerBase player, Object targetObj)
    {
        if (!player)
            return false;
        if (!targetObj)
            return false;

        vector playerPos = player.GetPosition();
        vector targetPos = targetObj.GetPosition();
        float maxDist = FLAG_ACTION_MAX_DISTANCE;
        float maxDistSq = maxDist * maxDist;
        float distSq = vector.DistanceSq(playerPos, targetPos);
        if (distSq > maxDistSq)
            return false;

        return true;
    }

    // The player belongs to the flag's group: the client reads its group cache (as the invite
    // action does), the server the group manager. Upgrades are offered only to members, so a
    // player of another group holding the upgrade tool keeps Raise/Lower on a stocked flag.
    static bool IsPlayerInFlagGroup(PlayerBase player, LFPG_FlagBase flag)
    {
        if (!player || !flag)
            return false;

        if (!GetGame().IsDedicatedServer())
        {
            if (!LFPG_ClientGroupCache.HasGroup())
                return false;
            return LFPG_ClientGroupCache.IsFlagAtPosition(flag.GetPosition());
        }

        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return false;

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return false;

        string groupID = mgr.GetPlayerGroupID(identity.GetPlainId());
        if (groupID == "")
            return false;

        return groupID == flag.GetGroupID();
    }

    // T2 -> T3 upgrade materials attached to the flag: 6 Firewood, 60 Nails, 10 Stones and a
    // vanilla flag that is not ruined. Shared by the upgrade action and by Raise/Lower, which
    // yield to the upgrade only when it would pass.
    static bool HasT3UpgradeMaterials(LFPG_FlagBase flag)
    {
        if (!flag)
            return false;

        string slotFW = "LFPG_FlagFirewood";
        ItemBase fwItem = ItemBase.Cast(flag.FindAttachmentBySlotName(slotFW));
        if (!fwItem || fwItem.GetQuantity() < 6)
            return false;

        string slotNails = "LFPG_FlagNails";
        ItemBase nailsItem = ItemBase.Cast(flag.FindAttachmentBySlotName(slotNails));
        if (!nailsItem || nailsItem.GetQuantity() < 60)
            return false;

        string slotStones = "LFPG_FlagStones";
        ItemBase stonesItem = ItemBase.Cast(flag.FindAttachmentBySlotName(slotStones));
        if (!stonesItem || stonesItem.GetQuantity() < 10)
            return false;

        string slotBanner = "Material_FPole_Flag";
        EntityAI banner = flag.FindAttachmentBySlotName(slotBanner);
        if (!banner || banner.IsRuined())
            return false;

        return true;
    }
};
