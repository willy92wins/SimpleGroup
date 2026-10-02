// ============================================================================
// LFPG_ActionJoinGroup.c - 4_World/actions
// Accion continua: mantener F para unirse al grupo via bandera en invite mode
// ActionContinuousBase con CAContinuousTime(2.0)
// Condicion: sin grupo + bandera en invite mode
// ============================================================================

class LFPG_ActionJoinGroupCB extends ActionContinuousBaseCB
{
    override void CreateActionComponent()
    {
        m_ActionData.m_ActionComponent = new CAContinuousTime(2.0);
    }
};

class LFPG_ActionJoinGroup extends ActionContinuousBase
{
    void LFPG_ActionJoinGroup()
    {
        m_CallbackClass = LFPG_ActionJoinGroupCB;
        // CMD_ACTIONFB_INTERACT: funciona con manos vacias (CCINone) en cualquier modelo.
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_INTERACT;
        m_FullBody = true;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;

        string text = "#STR_LFPG_ACTION_JOIN_GROUP";
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

        // Bandera debe estar en invite mode
        if (!flag.IsInviteModeActive())
            return false;

        // Jugador NO debe tener grupo
        if (!GetGame().IsDedicatedServer())
        {
            if (LFPG_ClientGroupCache.HasGroup())
                return false;
        }
        else
        {
            // FIX AUDIT: Validacion server-side - evita hold de 2s que falla silenciosamente
            PlayerIdentity identity = player.GetIdentity();
            if (identity)
            {
                LFPG_GroupManager mgr = LFPG_GroupManager.Get();
                if (mgr && mgr.HasGroup(identity.GetPlainId()))
                    return false;
            }
        }

        return true;
    }

    override void OnFinishProgressServer(ActionData action_data)
    {
        if (!action_data || !action_data.m_Target || !action_data.m_Player)
            return;
        if (!LFPG_ActionGuards.IsPlayerNearTarget(action_data.m_Player, action_data.m_Target.GetObject()))
            return;
        if (!ActionCondition(action_data.m_Player, action_data.m_Target, action_data.m_MainItem))
            return;

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

        // FIX G-16: Re-validar condiciones que pudieron cambiar durante el hold de 2s
        if (!flag.IsInviteModeActive())
        {
            mgr.SendErrorToPlayer(identity, player, "#STR_LFPG_ERR_INVITE_EXPIRED");
            return;
        }

        string groupID = flag.GetGroupID();
        if (groupID == "")
        {
            mgr.SendErrorToPlayer(identity, player, "#STR_LFPG_ERR_JOIN_FAILED");
            return;
        }

        // FIX G-16: Re-validar que el grupo sigue existiendo (podria haberse disuelto durante los 2s)
        if (!mgr.GroupExists(groupID))
        {
            mgr.SendErrorToPlayer(identity, player, "#STR_LFPG_ERR_JOIN_FAILED");
            return;
        }

        bool added = mgr.AddMember(groupID, playerUID, playerName);
        if (!added)
        {
            // FIX AUDIT: Enviar feedback al cliente cuando join falla
            mgr.SendErrorToPlayer(identity, player, "#STR_LFPG_ERR_JOIN_FAILED");
            return;
        }
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
