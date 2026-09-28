// ============================================================================
// LFPG_ActionUpgradeT3.c - 4_World/actions
// Upgrade T2 -> T3: Pickaxe + 6 Firewood + 60 Nails + 10 Stones
// ActionContinuousBase: mantener F (patron vanilla ActionBuildPart)
// Client usa Cache, server valida en OnFinishProgressServer
// ============================================================================

class LFPG_ActionUpgradeT3CB extends ActionContinuousBaseCB
{
    override void CreateActionComponent()
    {
        m_ActionData.m_ActionComponent = new CAContinuousTime(3.0);
    }
};

class LFPG_ActionUpgradeT3 extends ActionContinuousBase
{
    void LFPG_ActionUpgradeT3()
    {
        m_CallbackClass = LFPG_ActionUpgradeT3CB;
        // CMD_ACTIONFB_INTERACT: universal, compatible con cualquier modelo custom.
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_INTERACT;
        m_FullBody = true;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;

        string text = "#STR_LFPG_ACTION_UPGRADE_T3";
        m_Text = text;
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

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!player || !target || !item)
            return false;

        // FIX I-8: via ToolMatcher
        if (!LFPG_IsPickaxe(item))
            return false;

        Object targetObj = target.GetObject();
        if (!targetObj)
            return false;

        LFPG_FlagBase flag = LFPG_FlagBase.Cast(targetObj);
        if (!flag)
            return false;

        if (flag.GetTier() != 2)
            return false;

        // Materiales en slots custom con cantidades minimas
        string slotFW = "LFPG_FlagFirewood";
        EntityAI fwAtt = flag.FindAttachmentBySlotName(slotFW);
        if (!fwAtt)
            return false;
        ItemBase fwItem = ItemBase.Cast(fwAtt);
        if (!fwItem || fwItem.GetQuantity() < 6)
            return false;

        string slotNails = "LFPG_FlagNails";
        EntityAI nailsAtt = flag.FindAttachmentBySlotName(slotNails);
        if (!nailsAtt)
            return false;
        ItemBase nailsItem = ItemBase.Cast(nailsAtt);
        if (!nailsItem || nailsItem.GetQuantity() < 60)
            return false;

        string slotStones = "LFPG_FlagStones";
        EntityAI stonesAtt = flag.FindAttachmentBySlotName(slotStones);
        if (!stonesAtt)
            return false;
        ItemBase stonesItem = ItemBase.Cast(stonesAtt);
        if (!stonesItem || stonesItem.GetQuantity() < 10)
            return false;

        // Server valida grupo en OnFinishProgressServer (patron LFPowerGrid)
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
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        string groupID = mgr.GetPlayerGroupID(playerUID);
        if (groupID == "")
            return;
        if (groupID != flag.GetGroupID())
            return;

        // Doble check materiales server-side
        string slotFW = "LFPG_FlagFirewood";
        EntityAI fwAtt = flag.FindAttachmentBySlotName(slotFW);
        if (!fwAtt)
            return;
        ItemBase fwItem = ItemBase.Cast(fwAtt);
        if (!fwItem || fwItem.GetQuantity() < 6)
            return;

        string slotNails = "LFPG_FlagNails";
        EntityAI nailsAtt = flag.FindAttachmentBySlotName(slotNails);
        if (!nailsAtt)
            return;
        ItemBase nailsItem = ItemBase.Cast(nailsAtt);
        if (!nailsItem || nailsItem.GetQuantity() < 60)
            return;

        string slotStones = "LFPG_FlagStones";
        EntityAI stonesAtt = flag.FindAttachmentBySlotName(slotStones);
        if (!stonesAtt)
            return;
        ItemBase stonesItem = ItemBase.Cast(stonesAtt);
        if (!stonesItem || stonesItem.GetQuantity() < 10)
            return;

        string newClass = "LFPG_Flag_T3";
        bool success = mgr.UpgradeFlag(groupID, newClass, flag);
        if (!success)
        {
            string errMsg = "Upgrade T2->T3 failed for group: ";
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
