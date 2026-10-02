// ============================================================================
// LFPG_FlagKit_T1.c - 4_World/entities
// Kit crafteable que se despliega como LFPG_Flag_T1
//
// Patron EXACTO de FenceKit vanilla:
//   - ItemBase con IsDeployable + IsBasebuildingKit
//   - OnPlacementComplete: spawn entity + HideAllSelections
//   - ActionDeployObject.OnEndServer auto-borra (IsBasebuildingKit=true)
//   - NO delete manual (el engine lo hace)
//
// Config: Inventory_Base (KitBase no existe como config class)
// ============================================================================

class LFPG_FlagKit_T1 extends ItemBase
{
    protected bool m_PlacementSucceeded;

    bool DidPlaceSuccessfully()
    {
        return m_PlacementSucceeded;
    }

    override bool IsDeployable()
    {
        return true;
    }

    // Clave: sin esto, ActionDeployObject.OnEndServer no borra el kit
    override bool IsBasebuildingKit()
    {
        return true;
    }

    // Sonido durante la barra de progreso de deploy
    // ActionDeployObject usa esto — sin soundset, el deploy puede fallar
    override string GetLoopDeploySoundset()
    {
        string snd = "Shelter_Site_Build_Loop_SoundSet";
        return snd;
    }

    override string GetDeploySoundset()
    {
        string snd = "putDown_FenceKit_SoundSet";
        return snd;
    }

    override bool PlacementCanBeRotated()
    {
        return true;
    }

    override bool DoPlacingHeightCheck()
    {
        return false;
    }

    override float HeightCheckOverride()
    {
        return 5.0;
    }

    override bool CanBePlaced(Man player, vector position)
    {
        if (!super.CanBePlaced(player, position))
            return false;

        PlayerBase pb = PlayerBase.Cast(player);
        if (!pb)
            return false;
        #ifdef SERVER
        PlayerIdentity identity = pb.GetIdentity();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!identity || !mgr || !mgr.CanMutateGroups())
            return false;
        // A placement predicate must never dissolve groups as a side effect.
        if (mgr.HasGroup(identity.GetPlainId()))
            return false;
        if (mgr.IsPositionInTerritory(position))
            return false;
        #else
        if (LFPG_ClientGroupCache.HasGroup())
            return false;
        #endif
        return true;
    }

    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);
        #ifdef SERVER
        m_PlacementSucceeded = false;
        PlayerBase pb = PlayerBase.Cast(player);
        if (!pb)
            return;
        PlayerIdentity identity = pb.GetIdentity();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!identity || !mgr)
            return;
        if (!mgr.CanMutateGroups())
        {
            mgr.SendGroupsUnavailable(pb);
            return;
        }
        if (!CanBePlaced(player, position))
        {
            mgr.SendErrorToPlayer(identity, pb, "#STR_LFPG_ERR_TERRITORY_BLOCKED");
            return;
        }

        Object obj = GetGame().CreateObjectEx("LFPG_Flag_T1", position, ECE_CREATEPHYSICS | ECE_PLACE_ON_SURFACE);
        LFPG_FlagBase flag = LFPG_FlagBase.Cast(obj);
        if (!flag)
        {
            LFPG_Log.Error("Failed to spawn territory flag; kit retained.");
            return;
        }
        flag.SetPosition(position);
        flag.SetOrientation(orientation);

        string playerUID = identity.GetPlainId();
        string tempName = LFPG_GroupData.GenerateTempName(playerUID);
        string groupID = mgr.CreateGroup(playerUID, identity.GetName(), tempName, flag);
        if (groupID == "")
        {
            flag.SetSkipDissolveOnDelete();
            GetGame().ObjectDelete(flag);
            mgr.SendErrorToPlayer(identity, pb, "#STR_LFPG_ERR_TERRITORY_BLOCKED");
            return;
        }

        mgr.SendOpenNameDialog(identity, flag, groupID);
        mgr.SendGroupSyncFull(identity, groupID, flag, pb);
        m_PlacementSucceeded = true;
        HideAllSelections();
        #endif
    }

    override void SetActions()
    {
        super.SetActions();
        AddAction(ActionTogglePlaceObject);
        AddAction(ActionDeployObject);
    }
};
