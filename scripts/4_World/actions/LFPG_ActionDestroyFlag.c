// ============================================================================
// LFPG_ActionDestroyFlag.c — 4_World/actions
// Acción: líder destruye la bandera con hatchet → disuelve grupo
// Condición: líder del grupo + Hatchet en manos
// ============================================================================

class LFPG_ActionDestroyFlag extends ActionInteractBase
{
    void LFPG_ActionDestroyFlag()
    {
        string text = "#STR_LFPG_ACTION_DESTROY_FLAG";
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

        // Necesita Hatchet en manos
        EntityAI itemInHands = player.GetHumanInventory().GetEntityInHands();
        if (!itemInHands)
            return false;

        string handType = itemInHands.GetType();
        if (handType != "Hatchet")
            return false;

        // Solo líder
        if (!IsDedicatedServer())
        {
            if (!LFPG_ClientGroupCache.IsLeader())
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

        string playerUID = identity.GetPlainId();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        string groupID = flag.GetGroupID();
        LFPG_GroupData group = mgr.GetGroupByPlayer(playerUID);
        if (!group || !group.IsLeader(playerUID))
            return;

        // Disolver grupo (notifica a todos los miembros)
        mgr.DissolveGroup(groupID);

        // Destruir la entidad bandera
        GetGame().ObjectDelete(flag);
    }
};
