// ============================================================================
// LFPG_ModdedKits.c - 4_World/modded
// Basebuilding kits: make the structure they spawn count against the group quota.
//
// FenceKit (fencekit.c:27-29), WatchtowerKit (watchtowerkit.c:23) and ShelterKit
// (shelterkit.c:25) spawn the structure with CreateObjectEx and never call
// OnPlacementComplete on it, so LFPG_ModdedBaseBuildingBase never runs for them.
// The kit itself early-returns in LFPG_ModdedItemBase, so nothing counted at all and
// a group could burst past its limit until the next scheduled recalibration
// (default 1800 s).
//
// Reconciling right after the spawn keeps the quota honest without deleting a
// structure the player already paid for. The next deploy attempt then sees the real
// count and is blocked normally.
// ============================================================================

class LFPG_KitQuota
{
    // Runs after the kit's own OnPlacementComplete, so the structure already exists.
    static void Reconcile(Man player)
    {
        #ifdef SERVER
        PlayerBase pb = PlayerBase.Cast(player);
        if (!pb)
            return;

        PlayerIdentity identity = pb.GetIdentity();
        if (!identity)
            return;

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        string groupID = mgr.GetPlayerGroupID(identity.GetPlainId());
        if (groupID == "")
            return;

        // RecalibrateGroup rescans the world around the flag, so the structure is counted
        // without tracking it by hand. QueueRecalibrate is synchronous
        // (LFPG_GroupManager.c:1056-1061).
        mgr.QueueRecalibrate(groupID);
        #endif
    }
};

modded class FenceKit
{
    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);
        LFPG_KitQuota.Reconcile(player);
    }
};

modded class WatchtowerKit
{
    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);
        LFPG_KitQuota.Reconcile(player);
    }
};

modded class ShelterKit
{
    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);
        LFPG_KitQuota.Reconcile(player);
    }
};
