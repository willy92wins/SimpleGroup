// ============================================================================
// LFPG_ActionRegisterTerritory.c - 4_World/actions
// Accion continua (1.5s): registrar grupo en bandera sin dueno
// Condicion: bandera sin grupo + jugador sin grupo
//
// FIX M-15: Convertida a ActionContinuousBase para dar feedback visual al jugador
//           (barra de progreso) en vez de dispararse instantaneamente con F.
// ============================================================================

class LFPG_ActionRegisterTerritoryCB extends ActionContinuousBaseCB
{
    override void CreateActionComponent()
    {
        m_ActionData.m_ActionComponent = new CAContinuousTime(1.5);
    }
};

class LFPG_ActionRegisterTerritory extends ActionContinuousBase
{
    void LFPG_ActionRegisterTerritory()
    {
        m_CallbackClass = LFPG_ActionRegisterTerritoryCB;
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_INTERACT;
        m_FullBody = true;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;

        string text = "#STR_LFPG_ACTION_REGISTER";
        m_Text = text;
    }

    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINone;
        m_ConditionTarget = new CCTCursor;
    }

    override typename GetInputType()
    {
        return ContinuousInteractActionInput;
    }

    override bool HasTarget()
    {
        return true;
    }

    override bool HasProgress()
    {
        return true;
    }

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!player || !target)
            return false;

        Object targetObj = target.GetObject();
        if (!targetObj)
            return false;

        LFPG_FlagBase flag = LFPG_FlagBase.Cast(targetObj);
        if (!flag)
            return false;

        if (!GetGame().IsDedicatedServer())
        {
            if (LFPG_ClientGroupCache.HasGroup())
                return false;
            if (flag.IsInviteModeActive())
                return false;
            // Si la bandera ya tiene miembros (SyncVar), ya tiene dueño
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

    override void OnFinishProgressServer(ActionData action_data)
    {
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

        // Limpiar grupo zombi antes de verificar HasGroup
        mgr.CleanupStaleGroupForPlayer(playerUID);

        if (mgr.HasGroup(playerUID))
            return;

        string tempName = LFPG_GroupData.GenerateTempName(playerUID);
        string groupID = mgr.CreateGroup(playerUID, playerName, tempName, flag);
        if (groupID == "")
            return;

        mgr.SendOpenNameDialog(identity, flag, groupID);
        mgr.SendGroupSyncFull(identity, groupID, flag, flag);

        string logMsg = "Player registered territory: ";
        logMsg = logMsg + playerUID;
        LFPG_Log.Info(logMsg);
    }

    override bool ActionConditionContinue(ActionData action_data)
    {
        if (!super.ActionConditionContinue(action_data))
            return false;

        if (!GetGame().IsDedicatedServer())
            return true;

        PlayerBase player = action_data.m_Player;
        if (!player)
            return false;
        if (!action_data.m_Target)
            return false;

        Object targetObj = action_data.m_Target.GetObject();
        if (!LFPG_ActionGuards.IsPlayerNearTarget(player, targetObj))
            return false;

        return true;
    }
};
