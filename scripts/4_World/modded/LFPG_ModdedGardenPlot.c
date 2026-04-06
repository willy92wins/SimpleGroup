// ============================================================================
// LFPG_ModdedGardenPlot.c - 4_World/modded
// Restriccion de GardenPlot: territorio + maximo configurable por bandera
// Modda GardenPlot (base), asi GardenPlotPolytunnel y GardenPlotGreenhouse heredan
//
// Usa counter O(1) del GroupManager (no proximity scan)
// ============================================================================

modded class GardenPlot
{
    override bool CanBePlaced(Man player, vector position)
    {
        // Checks vanilla (superficie fertil, etc.)
        if (!super.CanBePlaced(player, position))
            return false;

        // Check de territorio: igual que cualquier construccion
        PlayerBase pb = PlayerBase.Cast(player);
        if (!pb)
            return false;

        PlayerIdentity identity = pb.GetIdentity();
        if (!identity)
            return false;

        string playerUID = identity.GetPlainId();

        #ifdef SERVER
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return false;

        // Check build zone
        if (!mgr.IsInBuildZone(playerUID, position))
            return false;

        // Check garden limit via counter O(1)
        string groupID = mgr.GetPlayerGroupID(playerUID);
        if (groupID == "")
            return false;

        if (!mgr.CanPlaceGarden(groupID))
            return false;
        #else
        // Client-side: check cache
        if (!LFPG_ClientGroupCache.HasGroup())
            return false;

        if (!LFPG_ClientGroupCache.IsInBuildZone(position))
            return false;
        #endif

        return true;
    }

    // Server: incrementar counter al colocar con exito
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
            mgr.IncrementGardenCount(groupID);
        }
        #endif
    }

    // Server: decrementar counter al destruir
    override void EEDelete(EntityAI parent)
    {
        #ifdef SERVER
        // Buscar a que grupo pertenece este garden plot por proximidad a banderas
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            vector pos = GetPosition();
            // Buscar la bandera mas cercana en build range
            string groupID = FindOwnerGroupID(mgr, pos);
            if (groupID != "")
            {
                mgr.DecrementGardenCount(groupID);
            }
        }
        #endif

        super.EEDelete(parent);
    }

    // Busca el grupo dueno por proximidad a su bandera
    protected string FindOwnerGroupID(LFPG_GroupManager mgr, vector pos)
    {
        if (!mgr)
            return "";
        return mgr.FindGroupIDAtPosition(pos);
    }
};
