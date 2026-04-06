// ============================================================================
// LFPG_ActionRegisterTerritory.c - 4_World/actions
// Accion: registrar grupo en bandera sin dueno (fallback para admin spawn)
// Condicion: bandera sin grupo + jugador sin grupo
// ============================================================================

class LFPG_ActionRegisterTerritory extends ActionInteractBase
{
    void LFPG_ActionRegisterTerritory()
    {
        string text = "#STR_LFPG_ACTION_REGISTER";
        m_Text = text;
    }

    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINone;
        m_ConditionTarget = new CCTObject(UAMaxDistances.DEFAULT);
    }


    override bool HasTarget()
    {
        return true;
    }

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!player || !target)
            return false;

        LFPG_FlagBase flag = LFPG_FlagBase.Cast(target.GetObject());
        if (!flag)
            return false;

        if (!GetGame().IsDedicatedServer())
        {
            if (LFPG_ClientGroupCache.HasGroup())
                return false;
            if (flag.IsInviteModeActive())
                return false;
            if (flag.GetMemberCount() > 0)
                return false;
            return true;
        }

        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return false;

        string playerUID = identity.GetPlainId();

        if (flag.HasGroup())
            return false;

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            if (mgr.HasGroup(playerUID))
                return false;
        }

        return true;
    }

    override void OnStartServer(ActionData action_data)
    {
        super.OnStartServer(action_data);

        LFPG_FlagBase flag = LFPG_FlagBase.Cast(action_data.m_Target.GetObject());
        if (!flag)
            return;

        PlayerBase player = action_data.m_Player;
        if (!player)
            return;

        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return;

        string playerUID = identity.GetPlainId();
        string playerName = identity.GetName();

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        if (flag.HasGroup())
            return;

        // FIX: Limpiar grupo zombi antes de verificar HasGroup
        mgr.CleanupStaleGroupForPlayer(playerUID);

        if (mgr.HasGroup(playerUID))
            return;

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
        if (groupID == "")
            return;

        mgr.SendOpenNameDialog(identity, flag, groupID);
        mgr.SendGroupSyncFull(identity, groupID, flag);

        string logMsg = "[SimpleGroup] Player registered territory: ";
        logMsg = logMsg + playerUID;
        Print(logMsg);
    }
};
