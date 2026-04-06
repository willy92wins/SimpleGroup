// ============================================================================
// LFPG_ActionDestroyFlag.c - 4_World/actions
// Accion: lider destruye la bandera con hatchet -> disuelve grupo
// FIX 3: Client usa Cache
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
        m_ConditionItem = new CCINonRuined;
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

        // Necesita Hatchet en manos (ambos lados)
        EntityAI itemInHands = player.GetHumanInventory().GetEntityInHands();
        if (!itemInHands)
            return false;

        string kindHatchet = "Hatchet";
        if (!itemInHands.IsKindOf(kindHatchet))
            return false;

        // FIX 3: Client-side usa SOLO el cache
        if (!GetGame().IsDedicatedServer())
        {
            if (!LFPG_ClientGroupCache.HasGroup())
                return false;

            if (!LFPG_ClientGroupCache.IsLeader())
                return false;

            if (!LFPG_ClientGroupCache.IsFlagAtPosition(flag.GetPosition()))
                return false;

            return true;
        }

        // Server-side
        if (!flag.HasGroup())
            return false;

        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return false;

        string playerUID = identity.GetPlainId();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return false;

        LFPG_GroupData group = mgr.GetGroupByPlayer(playerUID);
        if (!group || !group.IsLeader(playerUID))
            return false;

        if (group.m_GroupID != flag.GetGroupID())
            return false;

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

        mgr.DissolveGroup(groupID);
        GetGame().ObjectDelete(flag);
    }
};
