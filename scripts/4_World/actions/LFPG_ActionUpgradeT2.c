// ============================================================================
// LFPG_ActionUpgradeT2.c - 4_World/actions
// Upgrade T1 -> T2: SledgeHammer + WoodenLog + Rope en slots
// FIX 3: Client usa Cache
// ============================================================================

class LFPG_ActionUpgradeT2 extends ActionInteractBase
{
    void LFPG_ActionUpgradeT2()
    {
        string text = "#STR_LFPG_ACTION_UPGRADE_T2";
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

        if (flag.GetTier() != 1)
            return false;

        // Necesita SledgeHammer en manos
        EntityAI itemInHands = player.GetHumanInventory().GetEntityInHands();
        if (!itemInHands)
            return false;

        string kindSledge = "SledgeHammer";
        if (!itemInHands.IsKindOf(kindSledge))
            return false;

        // Verificar materiales en slots custom
        string slotLog = "LFPG_FlagLog";
        EntityAI logAtt = flag.FindAttachmentBySlotName(slotLog);
        if (!logAtt)
            return false;

        string slotRope = "LFPG_FlagRope";
        EntityAI ropeAtt = flag.FindAttachmentBySlotName(slotRope);
        if (!ropeAtt)
            return false;

        // FIX 3: Client-side usa SOLO el cache
        if (!GetGame().IsDedicatedServer())
        {
            if (!LFPG_ClientGroupCache.HasGroup())
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
        if (mgr)
        {
            string groupID = mgr.GetPlayerGroupID(playerUID);
            if (groupID == "")
                return false;
            if (groupID != flag.GetGroupID())
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

        string groupID = mgr.GetPlayerGroupID(playerUID);
        if (groupID == "")
            return;
        if (groupID != flag.GetGroupID())
            return;

        // Doble check materiales server-side
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
            string errMsg = "[SimpleGroup] Upgrade T1->T2 failed for group: ";
            errMsg = errMsg + groupID;
            Print(errMsg);
        }
    }
};
