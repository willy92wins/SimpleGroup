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
    override bool IsDeployable()
    {
        return true;
    }

    // Clave: sin esto, ActionDeployObject.OnEndServer no borra el kit
    override bool IsBasebuildingKit()
    {
        return true;
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

        // Server-side: verificar overlap de territorio via GroupManager
        #ifdef SERVER
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            if (mgr.IsPositionInTerritory(position))
                return false;
        }
        #endif

        return true;
    }

    // Patron FenceKit: spawn entity + HideAllSelections
    // ActionDeployObject.OnEndServer borrara el kit (IsBasebuildingKit=true)
    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);

        #ifdef SERVER
            // Obtener UID del jugador primero (necesario para cleanup y creacion)
            PlayerBase pb = PlayerBase.Cast(player);
            if (!pb)
                return;

            PlayerIdentity identity = pb.GetIdentity();
            if (!identity)
                return;

            string playerUID = identity.GetPlainId();
            string playerName = identity.GetName();

            LFPG_GroupManager mgr = LFPG_GroupManager.Get();
            if (!mgr)
                return;

            // FIX: Limpiar grupo zombi si la bandera del jugador fue destruida
            // pero el grupo sobrevivio (EEDelete fallo, admin delete, etc.)
            // Esto limpia m_PlayerToGroup, m_Groups, m_FlagPositions, m_GroupNames
            mgr.CleanupStaleGroupForPlayer(playerUID);

            // Validar overlap de territorio server-side
            if (mgr.IsPositionInTerritory(position))
            {
                string overlapMsg = "[SimpleGroup] Territory overlap rejected server-side at ";
                overlapMsg = overlapMsg + position.ToString();
                Print(overlapMsg);
                // No se puede evitar que ActionDeployObject borre el kit,
                // pero NO spawnear la bandera — el jugador pierde el kit
                // TODO: Notificar al jugador via RPC
                return;
            }

            // Spawnar bandera T1
            string flagClass = "LFPG_Flag_T1";
            Object obj = GetGame().CreateObjectEx(flagClass, position, ECE_PLACE_ON_SURFACE);
            LFPG_FlagBase flag = LFPG_FlagBase.Cast(obj);
            if (!flag)
            {
                string errMsg = "[SimpleGroup] ERROR: Failed to spawn LFPG_Flag_T1 at ";
                errMsg = errMsg + position.ToString();
                Print(errMsg);
                return;
            }

            flag.SetPosition(position);
            flag.SetOrientation(orientation);

            // Auto-registrar al jugador como dueno
            if (!mgr.HasGroup(playerUID))
            {
                string tempName = "#TEMP#";
                int uidLen = playerUID.Length();
                if (uidLen > 4)
                {
                    int startIdx = uidLen - 4;
                    string suffix = playerUID.Substring(startIdx, 4);
                    tempName = tempName + suffix;
                }
                else
                {
                    tempName = tempName + playerUID;
                }

                string groupID = mgr.CreateGroup(playerUID, playerName, tempName, flag);
                if (groupID != "")
                {
                    mgr.SendOpenNameDialog(identity, flag, groupID);
                    mgr.SendGroupSyncFull(identity, groupID, flag);

                    string logMsg = "[SimpleGroup] Territory placed + auto-registered: ";
                    logMsg = logMsg + playerUID;
                    Print(logMsg);
                }
            }
            else
            {
                string warnMsg = "[SimpleGroup] Player already has group, flag placed but no group created: ";
                warnMsg = warnMsg + playerUID;
                Print(warnMsg);
            }

            // Patron FenceKit: ocultar el kit, NO borrarlo manualmente
            // ActionDeployObject.OnEndServer se encarga del Delete
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
