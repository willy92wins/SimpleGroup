// ============================================================================
// LFPG_ModdedBaseBuildingBase.c — 4_World/modded
// Intercepta construcción y destrucción de objetos basebuilding
// para mantener el counter de deployed objects O(1)
//
// OnPlacementComplete: incrementar counter del grupo
// EEDelete: decrementar counter del grupo (busca owner por proximidad)
// ============================================================================

modded class BaseBuildingBase
{
    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);

        #ifdef SERVER
        PlayerBase pb = PlayerBase.Cast(player);
        if (!pb)
            return;

        PlayerIdentity identity = pb.GetIdentity();
        if (!identity)
            return;

        string playerUID = identity.GetPlainId();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        string groupID = mgr.GetPlayerGroupID(playerUID);
        if (groupID != "")
        {
            mgr.IncrementDeployCount(groupID);
        }
        #endif
    }

    override void EEDelete(EntityAI parent)
    {
        #ifdef SERVER
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            vector pos = GetPosition();
            string groupID = mgr.FindGroupIDAtPosition(pos);
            if (groupID != "")
            {
                mgr.DecrementDeployCount(groupID);
            }
        }
        #endif

        super.EEDelete(parent);
    }
};
