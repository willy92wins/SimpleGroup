// ============================================================================
// LFPG_ModdedGardenPlot.c - 4_World/modded
// Restriccion de GardenPlot: territorio + maximo configurable por bandera
// Modda GardenPlot (base), asi GardenPlotPolytunnel y GardenPlotGreenhouse heredan
//
// Usa counter O(1) del GroupManager (no proximity scan)
//
// EnablePlots=true  -> plots cuentan como garden (limite separado)
// EnablePlots=false -> plots cuentan como mueble (deploy limit)
//
// The server-side gate lives in LFPG_GardenPlotGate because the two greenhouse
// subclasses override OnPlacementComplete WITHOUT calling super
// (gardenplot.c:144-150, :171-177), so inheritance alone cannot carry it.
//
// FIX G-2: Rechaza plots en territorio ajeno
// FIX D-6: Re-valida IsInBuildZone server-side en OnPlacementComplete
// FIX C-3: Idempotencia via IsTracked/IsGardenTracked
// FIX PLACEMENT:
//   - CanBePlaced cliente alineado con Hologram (anade IsNearOtherTerritory)
//   - OnPlacementComplete server envia error al jugador via SendErrorToPlayer
//     para cada caso de fallo, ademas de logear y borrar la entidad.
// ============================================================================

class LFPG_GardenPlotGate
{
    // Server-side territory gate for garden plots. Called explicitly by GardenPlot and by
    // its two greenhouse subclasses, which override OnPlacementComplete without super.
    static void Apply(GardenPlot plot, Man player, vector position)
    {
        #ifdef SERVER
        if (!plot)
            return;

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

        // ActionDigGardenPlot (actiondiggardenplot.c:119) and ActionCreateGreenhouseGardenPlot
        // (actioncreategreenhousegardenplot.c:110) call OnPlacementComplete with the player
        // only, so position defaults to the map origin and every check below would run
        // against (0,0,0). The plot already sits at its final spot by then.
        // Scoped to garden plots on purpose: on the normal deploy path
        // (actiondeploybase.c:127) position is the TARGET spot while GetPosition() still
        // returns the pre-move one, so the same fallback would break placement there.
        vector effPos = position;
        if (effPos[0] == 0 && effPos[2] == 0)
            effPos = plot.GetPosition();

        // v3+: items en Lista A o B se auto-gestionan, skip conteo
        LFPG_TerritoryConfig cfgGp = mgr.GetConfig();
        if (cfgGp && (cfgGp.IsNoBaseRequired(plot) || cfgGp.IsUnrestricted(plot)))
            return;

        string groupID = mgr.GetPlayerGroupID(playerUID);
        if (groupID == "")
        {
            LFPG_Log.Error("Unauthorized GardenPlot by " + playerUID + " - no group. Deleting.");
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_NO_GROUP));
            GetGame().ObjectDelete(plot);
            return;
        }

        // FIX D-6: Re-valida territorio server-side (CanBePlaced podria haberse bypaseado)
        if (!mgr.IsInBuildZone(playerUID, effPos))
        {
            LFPG_Log.Error("GardenPlot outside zone by " + playerUID + ". Deleting.");
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_OUTSIDE_ZONE));
            GetGame().ObjectDelete(plot);
            return;
        }

        // FIX G-2: Territorio ajeno
        if (mgr.IsPositionInOtherTerritory(effPos, groupID))
        {
            LFPG_Log.Error("GardenPlot in other territory by " + playerUID + ". Deleting.");
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_OTHER_TERRITORY));
            GetGame().ObjectDelete(plot);
            return;
        }

        LFPG_TerritoryConfig cfg = mgr.GetConfig();
        if (cfg && cfg.m_EnablePlots)
        {
            if (!mgr.CanPlaceGarden(groupID))
            {
                LFPG_Log.Error("Garden limit reached for group " + groupID);
                mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_GARDEN_LIMIT));
                GetGame().ObjectDelete(plot);
                return;
            }
            // FIX C-3: Idempotencia
            if (LFPG_DeployTracker.IsGardenTracked(plot))
                return;
            mgr.IncrementGardenCount(groupID);
            LFPG_DeployTracker.TrackGarden(plot, groupID);
        }
        else
        {
            if (!mgr.CanDeploy(groupID))
            {
                LFPG_Log.Error("Deploy limit reached for plot-as-furniture: " + groupID);
                mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_DEPLOY_LIMIT));
                GetGame().ObjectDelete(plot);
                return;
            }
            // FIX C-3: Idempotencia
            if (LFPG_DeployTracker.IsTracked(plot))
                return;
            mgr.IncrementDeployCount(groupID);
            LFPG_DeployTracker.Track(plot, groupID);
        }

        #endif
    }
};

modded class GardenPlot
{
    override bool CanBePlaced(Man player, vector position)
    {
        // Checks vanilla (superficie fertil, etc.)
        if (!super.CanBePlaced(player, position))
            return false;

        // ActionDigGardenPlot probes the four plot corners with CanBePlaced(NULL, ...)
        // (actiondiggardenplot.c:73-79). Vanilla ignores the Man there and only tests the
        // surface (gardenplot.c:103-111), so rejecting a null player kept
        // SetIsCollidingGPlot(false) from ever running: the hologram stayed red and the
        // shovel action never became available. super already passed above.
        PlayerBase pb = PlayerBase.Cast(player);
        if (!pb)
            return true;

        if (GetGame().IsDedicatedServer())
        {
            PlayerIdentity identity = pb.GetIdentity();
            if (!identity)
                return false;

            string playerUID = identity.GetPlainId();

            LFPG_GroupManager mgr = LFPG_GroupManager.Get();
            if (!mgr)
                return false;

            LFPG_TerritoryConfig cfg = mgr.GetConfig();

            // v3+: Lista B sin restriccion (prio sobre A)
            if (cfg && cfg.IsUnrestricted(this))
                return true;

            string groupID = mgr.GetPlayerGroupID(playerUID);

            // v3+: Lista A permite sin grupo, solo bloquea en ajeno
            if (cfg && cfg.IsNoBaseRequired(this))
            {
                if (groupID != "")
                    return !mgr.IsPositionInOtherTerritory(position, groupID);
                return !mgr.IsPositionInTerritory(position);
            }

            if (groupID == "")
                return false;

            if (!mgr.IsInBuildZone(playerUID, position))
                return false;

            // FIX G-2: Rechaza si cae dentro de territorio de otro grupo
            if (mgr.IsPositionInOtherTerritory(position, groupID))
                return false;

            // Plots activos: check garden limit. Desactivos: check deploy limit.
            if (cfg && cfg.m_EnablePlots)
            {
                if (!mgr.CanPlaceGarden(groupID))
                    return false;
            }
            else
            {
                if (!mgr.CanDeploy(groupID))
                    return false;
            }
        }
        else
        {
            // Client: usar cache O(1). Alineado con Hologram.EvaluateCollision
            // v3+: Lista B sin restriccion (prio sobre A)
            if (LFPG_ClientGroupCache.IsUnrestrictedCached(this))
                return true;

            // v3+: Lista A permite sin grupo, solo bloquea cerca de territorio ajeno
            if (LFPG_ClientGroupCache.IsNoBaseRequiredCached(this))
                return !LFPG_ClientGroupCache.IsNearOtherTerritory(position);

            if (!LFPG_ClientGroupCache.HasGroup())
                return false;

            if (!LFPG_ClientGroupCache.IsInBuildZone(position))
                return false;

            // FIX PLACEMENT: consistencia con Hologram (G-2 client-side)
            if (LFPG_ClientGroupCache.IsNearOtherTerritory(position))
                return false;

            if (LFPG_ClientGroupCache.s_EnablePlots)
            {
                if (!LFPG_ClientGroupCache.CanPlaceGarden())
                    return false;
            }
            else
            {
                if (!LFPG_ClientGroupCache.CanDeploy())
                    return false;
            }
        }

        return true;
    }

    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);
        LFPG_GardenPlotGate.Apply(this, player, position);
    }

    override void EEDelete(EntityAI parent)
    {
        #ifdef SERVER
        // Intentar untrack garden primero, luego deploy
        string gardenGroup = LFPG_DeployTracker.UntrackGarden(this);
        if (gardenGroup != "")
        {
            LFPG_GroupManager mgr = LFPG_GroupManager.Get();
            if (mgr)
            {
                mgr.DecrementGardenCount(gardenGroup);
            }
        }
        else
        {
            // Podria estar trackeado como deploy (EnablePlots=false)
            string deployGroup = LFPG_DeployTracker.Untrack(this);
            if (deployGroup != "")
            {
                LFPG_GroupManager mgr2 = LFPG_GroupManager.Get();
                if (mgr2)
                {
                    mgr2.DecrementDeployCount(deployGroup);
                }
            }
        }
        #endif

        super.EEDelete(parent);
    }
};

modded class GardenPlotPolytunnel
{
    // Vanilla stops at SyncSlots and never calls super (gardenplot.c:144-150), so the
    // GardenPlot gate never ran for this subclass: no group check, no zone check, no count.
    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);
        LFPG_GardenPlotGate.Apply(this, player, position);
    }
};

modded class GardenPlotGreenhouse
{
    // Same shape as GardenPlotPolytunnel (gardenplot.c:171-177).
    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);
        LFPG_GardenPlotGate.Apply(this, player, position);
    }
};
