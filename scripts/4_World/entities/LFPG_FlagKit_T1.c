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

        // FIX AUDIT: Jugador con grupo existente no puede colocar otra bandera
        PlayerBase pb = PlayerBase.Cast(player);
        if (pb)
        {
            #ifdef SERVER
            PlayerIdentity identity = pb.GetIdentity();
            if (identity)
            {
                LFPG_GroupManager mgr = LFPG_GroupManager.Get();
                if (mgr)
                {
                    // Limpiar grupo zombi primero (defensivo)
                    mgr.CleanupStaleGroupForPlayer(identity.GetPlainId());

                    if (mgr.HasGroup(identity.GetPlainId()))
                        return false;

                    if (mgr.IsPositionInTerritory(position))
                        return false;
                }
            }
            #else
            if (LFPG_ClientGroupCache.HasGroup())
                return false;
            #endif
        }

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
                string overlapMsg = "Territory overlap rejected server-side at ";
                overlapMsg = overlapMsg + position.ToString();
                LFPG_Log.Error(overlapMsg);

                // Notificar al jugador via RPC (ERROR_MSG via PlayerBase)
                ScriptRPC errRpc = new ScriptRPC();
                string errKey = "#STR_LFPG_ERR_TERRITORY_BLOCKED";
                errRpc.Write(errKey);
                errRpc.Send(pb, LFPG_RPC_S2C_ERROR_MSG, true, identity);
                return;
            }

            // Spawnar bandera T1 en la posicion del hologram
            string flagClass = "LFPG_Flag_T1";
            Object obj = GetGame().CreateObjectEx(flagClass, position, ECE_CREATEPHYSICS | ECE_PLACE_ON_SURFACE);
            LFPG_FlagBase flag = LFPG_FlagBase.Cast(obj);
            if (!flag)
            {
                string errMsg = "Failed to spawn LFPG_Flag_T1 at ";
                errMsg = errMsg + position.ToString();
                LFPG_Log.Error(errMsg);
                return;
            }

            flag.SetPosition(position);
            flag.SetOrientation(orientation);

            // Auto-registrar al jugador como dueno
            if (!mgr.HasGroup(playerUID))
            {
                string tempName = LFPG_GroupData.GenerateTempName(playerUID);
                string groupID = mgr.CreateGroup(playerUID, playerName, tempName, flag);
                if (groupID != "")
                {
                    mgr.SendOpenNameDialog(identity, flag, groupID);
                    mgr.SendGroupSyncFull(identity, groupID, flag, flag);

                    // Fallback fiable: enviar tambien via PlayerBase (siempre existe en client).
                    // GROUP_SYNC_FULL via flag puede perderse si la entidad flag
                    // no se ha replicado al cliente aun (race condition de JIP).
                    // LIGHTWEIGHT_SYNC via PlayerBase garantiza que s_HasGroup=true.
                    mgr.SendLightweightSync(identity, groupID, flag, pb);

                    string logMsg = "Territory placed + auto-registered: ";
                    logMsg = logMsg + playerUID;
                    LFPG_Log.Info(logMsg);
                }
            }
            else
            {
                string warnMsg = "Player already has group, flag placed but no group created: ";
                warnMsg = warnMsg + playerUID;
                LFPG_Log.Error(warnMsg);
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
