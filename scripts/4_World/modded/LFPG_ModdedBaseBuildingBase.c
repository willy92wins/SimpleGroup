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
            LFPG_TerritoryConfig config = mgr.GetConfig();
            if (config)
            {
                vector pos = GetPosition();
                int i;
                int count = mgr.m_FlagPositions.Count();
                for (i = 0; i < count; i = i + 1)
                {
                    LFPG_FlagPositionCache cache = mgr.m_FlagPositions[i];
                    if (!cache)
                        continue;

                    float distSq = vector.DistanceSq(pos, cache.m_Position);
                    if (distSq < config.m_BuildRadiusSq)
                    {
                        mgr.DecrementDeployCount(cache.m_GroupID);
                        break;
                    }
                }
            }
        }
        #endif

        super.EEDelete(parent);
    }
};
