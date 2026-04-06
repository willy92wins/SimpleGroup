// ============================================================================
// LFPG_ActionJoinGroup.c - 4_World/actions
// Accion: unirse al grupo via bandera en invite mode
// Condicion: sin grupo + bandera en invite mode + grupo no lleno
// ============================================================================

class LFPG_ActionJoinGroup extends ActionInteractBase
{
    void LFPG_ActionJoinGroup()
    {
        string text = "#STR_LFPG_ACTION_JOIN_GROUP";
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

        // Bandera debe estar en invite mode
        if (!flag.IsInviteModeActive())
            return false;

        // Jugador NO debe tener grupo
        if (!GetGame().IsDedicatedServer())
        {
            if (LFPG_ClientGroupCache.HasGroup())
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

        string groupID = flag.GetGroupID();
        if (groupID == "")
            return;

        bool added = mgr.AddMember(groupID, playerUID, playerName);
        if (!added)
        {
            // Join failed - group full, already member, etc.
            return;
        }
        // AddMember already sends GROUP_SYNC_FULL to all members including the new one
    }
};
