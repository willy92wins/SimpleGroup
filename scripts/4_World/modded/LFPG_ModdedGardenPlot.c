// ============================================================================
// LFPG_ModdedGardenPlot.c — 4_World/modded
// Restricción de GardenPlot: territorio + máximo configurable por bandera
// Modda GardenPlot (base), así GardenPlotPolytunnel y GardenPlotGreenhouse heredan
//
// Usa counter O(1) del GroupManager (no proximity scan)
// ============================================================================

modded class GardenPlot
{
    override bool CanBePlaced(Man player, vector position)
    {
        // Checks vanilla (superficie fértil, etc.)
        if (!super.CanBePlaced(player, position))
            return false;

        // Check de territorio: igual que cualquier construcción
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

    // Server: incrementar counter al colocar con éxito
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
        // Buscar a qué grupo pertenece este garden plot por proximidad a banderas
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            vector pos = GetPosition();
            // Buscar la bandera más cercana en build range
            string groupID = FindOwnerGroupID(mgr, pos);
            if (groupID != "")
            {
                mgr.DecrementGardenCount(groupID);
            }
        }
        #endif

        super.EEDelete(parent);
    }

    // Busca el grupo dueño por proximidad a su bandera
    protected string FindOwnerGroupID(LFPG_GroupManager mgr, vector pos)
    {
        if (!mgr)
            return "";

        LFPG_TerritoryConfig config = mgr.GetConfig();
        if (!config)
            return "";

        // Iterar grupos y buscar la bandera cuyo build radius contiene esta posición
        // Esto es O(f) pero solo se ejecuta al destruir un garden (raro)
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
                return cache.m_GroupID;
            }
        }
        return "";
    }
};
