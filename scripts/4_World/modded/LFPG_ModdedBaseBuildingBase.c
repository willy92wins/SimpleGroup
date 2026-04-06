// ============================================================================
// LFPG_ModdedBaseBuildingBase.c - 4_World/modded
// Intercepta construccion y destruccion de objetos basebuilding
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

        // FIX H1: Validar server-side que el jugador puede construir aqui
        // Si no tiene grupo o no esta en build zone -> borrar objeto (anti-cheat)
        if (groupID == "")
        {
            string errMsg1 = "[SimpleGroup] Unauthorized build by ";
            errMsg1 = errMsg1 + playerUID;
            errMsg1 = errMsg1 + " - no group. Deleting.";
            Print(errMsg1);
            mgr.SuppressNextDeployDecrement();
            GetGame().ObjectDelete(this);
            return;
        }

        if (!mgr.IsInBuildZone(playerUID, position))
        {
            string errMsg2 = "[SimpleGroup] Build outside zone by ";
            errMsg2 = errMsg2 + playerUID;
            errMsg2 = errMsg2 + ". Deleting.";
            Print(errMsg2);
            mgr.SuppressNextDeployDecrement();
            GetGame().ObjectDelete(this);
            return;
        }

        if (!mgr.CanDeploy(groupID))
        {
            string errMsg3 = "[SimpleGroup] Deploy limit reached for group ";
            errMsg3 = errMsg3 + groupID;
            errMsg3 = errMsg3 + ". Deleting.";
            Print(errMsg3);
            mgr.SuppressNextDeployDecrement();
            GetGame().ObjectDelete(this);
            return;
        }

        mgr.IncrementDeployCount(groupID);
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
            else
            {
                // Objeto fuera de toda build zone: limpiar suppress flag si estaba activo
                mgr.ClearDeployDecrementSuppress();
            }
        }
        #endif

        super.EEDelete(parent);
    }
};
