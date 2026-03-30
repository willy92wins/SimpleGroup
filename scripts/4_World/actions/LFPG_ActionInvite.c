// ============================================================================
// LFPG_ActionInvite.c — 4_World/actions
// Acción single: activar modo invitación en la bandera
// Condición: miembro del grupo + bandera subida
// El RPC real va al GroupManager via la bandera
// ============================================================================

class LFPG_ActionInvite extends ActionInteractBase
{
    void LFPG_ActionInvite()
    {
        string text = "#STR_LFPG_ACTION_INVITE";
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

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!player || !target)
            return false;

        LFPG_FlagBase flag = LFPG_FlagBase.Cast(target.GetObject());
        if (!flag)
            return false;

        if (!flag.HasGroup())
            return false;

        // Invite mode ya activo → no mostrar
        if (flag.IsInviteModeActive())
            return false;

        // Bandera debe estar subida (progress > 0)
        if (flag.IsFullyLowered())
            return false;

        // Solo miembros del grupo
        if (!IsDedicatedServer())
        {
            if (!LFPG_ClientGroupCache.HasGroup())
                return false;
            if (LFPG_ClientGroupCache.s_GroupID != flag.GetGroupID())
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

        // Enviar RPC al manager
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        LFPG_TerritoryConfig config = mgr.GetConfig();
        if (!config)
            return;

        int durationMs = config.m_InviteDurationSeconds * 1000;
        flag.ActivateInviteMode(durationMs);
    }
};
