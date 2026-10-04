// ============================================================================
// LFPG_ModdedBaseBuildingBase.c - 4_World/modded
// Intercepta construccion y destruccion de objetos basebuilding
// para mantener el counter de deployed objects O(1)
//
// FIX D-5: CanBePlaced override como defensa en profundidad (admin tools, etc.)
// FIX G-2: Bloquea tambien builds dentro de territorio ajeno
// FIX C-3: Idempotencia via IsTracked antes de incrementar
// FIX D-11: Comentario stale sobre SuppressNextDeployDecrement removido
// FIX PLACEMENT:
//   - CanBePlaced cliente alineado con Hologram (anade IsNearOtherTerritory)
//   - OnPlacementComplete server envia error al jugador via SendErrorToPlayer
// ============================================================================

modded class BaseBuildingBase
{
    // FIX D-5: Defensa en profundidad - CanBePlaced se llama por Hologram client-side
    // y tambien por el pipeline vanilla. Si el placement bypasea el hologram modded
    // (admin tools, mods con placement propio), aqui sigue bloqueando.
    override bool CanBePlaced(Man player, vector position)
    {
        if (!super.CanBePlaced(player, position))
            return false;

        PlayerBase pb = PlayerBase.Cast(player);
        if (!pb)
            return false;

        // Server side: usar GroupManager
        if (GetGame().IsDedicatedServer())
        {
            PlayerIdentity identity = pb.GetIdentity();
            if (!identity)
                return false;
            string playerUID = identity.GetPlainId();

            LFPG_GroupManager mgr = LFPG_GroupManager.Get();
            if (!mgr)
                return false;

            // v3+: Lista B sin restriccion (prio sobre A)
            LFPG_TerritoryConfig cfgSrv = mgr.GetConfig();
            if (LFPG_IsListedDropBlocked(this, pb, position))
                return false;
            if (cfgSrv && LFPG_IsFurniturePlacementExempt(this, cfgSrv.m_FurnitureExcludedTypes))
                return true;
            if (cfgSrv && cfgSrv.IsUnrestricted(this))
                return true;

            string groupID = mgr.GetPlayerGroupID(playerUID);

            // v3+: Lista A permite sin grupo, solo bloquea en ajeno
            if (cfgSrv && cfgSrv.IsNoBaseRequired(this))
            {
                if (groupID != "")
                    return !mgr.IsPositionInOtherTerritory(position, groupID);
                return !mgr.IsPositionInTerritory(position);
            }

            if (groupID == "")
                return false;

            if (!mgr.IsInBuildZone(playerUID, position))
                return false;

            // FIX G-2: Rechaza si la posicion esta dentro del territorio de otro grupo
            if (mgr.IsPositionInOtherTerritory(position, groupID))
                return false;

            if (!mgr.CanDeploy(groupID))
                return false;
        }
        else
        {
            if (!LFPG_ClientGroupCache.s_PlacementRulesReceived)
                return true;
            if (LFPG_ClientGroupCache.IsFurniturePlacementExemptCached(this))
                return true;
            // Client side: usar cache O(1). Alineado con Hologram.EvaluateCollision.
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
            if (!LFPG_ClientGroupCache.CanDeploy())
                return false;
        }

        return true;
    }

    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);

        #ifdef SERVER
        // Excluir explicitamente entidades LFPG (defensivo - las flags extienden
        // ItemBase, no BBB, pero kits o modded chain podrian pasar por aqui)
        string lfpgFlag = "LFPG_FlagBase";
        if (IsKindOf(lfpgFlag))
            return;

        // v3+: items en Lista A o B gestionan su placement fuera de este flujo
        LFPG_GroupManager mgrBbbCfg = LFPG_GroupManager.Get();
        if (mgrBbbCfg)
        {
            LFPG_TerritoryConfig cfgBbbEarly = mgrBbbCfg.GetConfig();
            if (cfgBbbEarly && LFPG_IsFurniturePlacementExempt(this, cfgBbbEarly.m_FurnitureExcludedTypes))
                return;
            if (cfgBbbEarly && (cfgBbbEarly.IsNoBaseRequired(this) || cfgBbbEarly.IsUnrestricted(this)))
                return;
        }

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

        // Validacion server-side (anti-cheat: si CanBePlaced fue bypaseado)
        if (groupID == "")
        {
            LFPG_Log.Error("Unauthorized build by " + playerUID + " - no group. Deleting.");
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_NO_GROUP));
            GetGame().ObjectDelete(this);
            return;
        }

        if (!mgr.IsInBuildZone(playerUID, position))
        {
            LFPG_Log.Error("Build outside zone by " + playerUID + ". Deleting.");
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_OUTSIDE_ZONE));
            GetGame().ObjectDelete(this);
            return;
        }

        // FIX G-2: Re-check territorio ajeno server-side
        if (mgr.IsPositionInOtherTerritory(position, groupID))
        {
            LFPG_Log.Error("Build in other territory by " + playerUID + ". Deleting.");
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_OTHER_TERRITORY));
            GetGame().ObjectDelete(this);
            return;
        }

        if (!mgr.CanDeploy(groupID))
        {
            LFPG_Log.Error("Deploy limit reached for group " + groupID);
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_DEPLOY_LIMIT));
            GetGame().ObjectDelete(this);
            return;
        }

        // FIX C-3: Idempotencia - si ya esta trackeado (re-entrada por bug), no incrementar
        if (LFPG_DeployTracker.IsTracked(this))
            return;

        string bbbMsg = "BBB Deploy counted: ";
        bbbMsg = bbbMsg + GetType();
        bbbMsg = bbbMsg + " for group ";
        bbbMsg = bbbMsg + groupID;
        LFPG_Log.Debug(bbbMsg);
        mgr.IncrementDeployCount(groupID);
        LFPG_DeployTracker.Track(this, groupID);
        #endif
    }

    override void EEDelete(EntityAI parent)
    {
        #ifdef SERVER
        string trackedGroup = LFPG_DeployTracker.Untrack(this);
        if (trackedGroup != "")
        {
            LFPG_GroupManager mgr = LFPG_GroupManager.Get();
            if (mgr)
            {
                mgr.DecrementDeployCount(trackedGroup);
            }
        }
        #endif

        super.EEDelete(parent);
    }
};
