// ============================================================================
// LFPG_ActionDestroyFlag.c - 4_World/actions
// Accion: lider destruye la bandera con hatchet -> disuelve grupo
// FIX 3: Client usa Cache
// ============================================================================

class LFPG_ActionDestroyFlagCB extends ActionContinuousBaseCB
{
    override void CreateActionComponent()
    {
        m_ActionData.m_ActionComponent = new CAContinuousTime(5.0);
    }
};

class LFPG_ActionDestroyFlag extends ActionContinuousBase
{
    void LFPG_ActionDestroyFlag()
    {
        m_CallbackClass = LFPG_ActionDestroyFlagCB;
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_INTERACT;
        m_FullBody = true;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;
        string text = "#STR_LFPG_ACTION_DESTROY_FLAG";
        m_Text = text;
    }

    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINonRuined;
        m_ConditionTarget = new CCTCursor(5.0);
    }

    override bool UseMainItem()
    {
        return true;
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

    override bool MainItemAlwaysInHands()
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

        // Necesita Hatchet/Axe en manos (ambos lados) — FIX M-14 via ToolMatcher
        EntityAI itemInHands = player.GetHumanInventory().GetEntityInHands();
        if (!itemInHands)
            return false;
        if (itemInHands != item || item.IsRuined())
            return false;

        if (!LFPG_IsHatchet(itemInHands))
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
        if (!LFPG_ActionGuards.IsPlayerNearTarget(player, targetObj))
            return false;

        if (!flag.HasGroup())
            return false;

        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return false;

        string playerUID = identity.GetPlainId();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr || !mgr.CanMutateGroups())
            return false;

        LFPG_GroupData group = mgr.GetGroupByPlayer(playerUID);
        if (!group || !group.IsLeader(playerUID))
            return false;

        if (group.m_GroupID != flag.GetGroupID())
            return false;

        return true;
    }

    override void OnFinishProgressServer(ActionData action_data)
    {
        if (!action_data || !action_data.m_Target || !action_data.m_MainItem)
            return;
        if (action_data.m_MainItem.IsRuined())
            return;
        if (!ActionCondition(action_data.m_Player, action_data.m_Target, action_data.m_MainItem))
            return;

        super.OnFinishProgressServer(action_data);

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

        if (group.m_GroupID != groupID)
            return;

        // Destroying a duplicate must never dissolve the real flag's group.
        if (mgr.GetGroupFlag(groupID) == flag)
            mgr.DissolveGroup(groupID);
        else
            mgr.ReleaseUnregisteredFlag(flag);
        flag.SetSkipDissolveOnDelete();
        GetGame().ObjectDelete(flag);
    }
};
