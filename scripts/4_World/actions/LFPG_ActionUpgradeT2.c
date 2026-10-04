// ============================================================================
// LFPG_ActionUpgradeT2.c - 4_World/actions
// Upgrade T1 -> T2: SledgeHammer + WoodenLog + Rope en slots
// Patron: LFPG_ActionUpgradeSolarPanel (LFPowerGrid v0.7.47)
// ============================================================================

class LFPG_ActionUpgradeT2CB extends ActionContinuousBaseCB
{
    override void CreateActionComponent()
    {
        m_ActionData.m_ActionComponent = new CAContinuousTime(3.0);
    }
};

class LFPG_ActionUpgradeT2 extends ActionContinuousBase
{
    void LFPG_ActionUpgradeT2()
    {
        m_CallbackClass = LFPG_ActionUpgradeT2CB;
        // Same animation vanilla plays when building with a sledgehammer (ActionBuildPart); it has no crouched variant.
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_MINEROCK;
        m_FullBody = true;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT;
        m_Text = "#STR_LFPG_ACTION_UPGRADE_T2";
    }

    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINonRuined;
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

    override bool SetupAction(PlayerBase player, ActionTarget target, ItemBase item, out ActionData action_data, Param extra_data = NULL)
    {
        if (!super.SetupAction(player, target, item, action_data, extra_data))
            return false;

        // Mine rock only fits the sledgehammer; other accepted hammers (mod mallets) keep the generic animation.
        string kSledge = "SledgeHammer";
        if (item && item.IsKindOf(kSledge))
            m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_MINEROCK;
        else
            m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_INTERACT;

        return true;
    }

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!player || !target || !item)
            return false;

        // FIX I-8: via ToolMatcher
        if (!LFPG_IsSledgeHammer(item))
            return false;

        Object targetObj = target.GetObject();
        if (!targetObj)
            return false;

        LFPG_FlagBase flag = LFPG_FlagBase.Cast(targetObj);
        if (!flag)
            return false;

        if (flag.GetTier() != 1)
            return false;

        string slotLog = "LFPG_FlagLog";
        EntityAI logAtt = flag.FindAttachmentBySlotName(slotLog);
        if (!logAtt)
            return false;

        string slotRope = "LFPG_FlagRope";
        EntityAI ropeAtt = flag.FindAttachmentBySlotName(slotRope);
        if (!ropeAtt)
            return false;

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

        super.OnFinishProgressServer(action_data);

        if (!action_data || !action_data.m_Target)
            return;

        Object targetObj = action_data.m_Target.GetObject();
        if (!targetObj)
            return;

        LFPG_FlagBase flag = LFPG_FlagBase.Cast(targetObj);
        if (!flag)
            return;

        if (flag.GetTier() != 1)
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
        if (!mgr.CanMutateGroups())
        {
            mgr.SendGroupsUnavailable(player);
            return;
        }

        string groupID = mgr.GetPlayerGroupID(playerUID);
        if (groupID == "")
            return;
        if (groupID != flag.GetGroupID())
            return;

        string slotLog = "LFPG_FlagLog";
        EntityAI logAtt = flag.FindAttachmentBySlotName(slotLog);
        if (!logAtt)
            return;

        string slotRope = "LFPG_FlagRope";
        EntityAI ropeAtt = flag.FindAttachmentBySlotName(slotRope);
        if (!ropeAtt)
            return;

        string newClass = "LFPG_Flag_T2";
        bool success = mgr.UpgradeFlag(groupID, newClass, flag);
        if (!success)
        {
            string errMsg = "Upgrade T1->T2 failed for group: ";
            errMsg = errMsg + groupID;
            LFPG_Log.Error(errMsg);
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
