// ============================================================================
// LFPG_ActionUpgradeT3.c - 4_World/actions
// Upgrade T2 -> T3: Pickaxe + 6 Firewood + 60 Nails + 16 Stones
// FIX 3: Client usa Cache
// ============================================================================

class LFPG_ActionUpgradeT3 extends ActionInteractBase
{
    void LFPG_ActionUpgradeT3()
    {
        string text = "#STR_LFPG_ACTION_UPGRADE_T3";
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

        if (flag.GetTier() != 2)
            return false;

        // Necesita Pickaxe en manos
        EntityAI itemInHands = player.GetHumanInventory().GetEntityInHands();
        if (!itemInHands)
            return false;

        string kindPickaxe = "Pickaxe";
        if (!itemInHands.IsKindOf(kindPickaxe))
            return false;

        // Verificar materiales en slots custom
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
        if (!stonesItem || stonesItem.GetQuantity() < 16)
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
        if (!stonesItem || stonesItem.GetQuantity() < 16)
            return;

        string newClass = "LFPG_Flag_T3";
        bool success = mgr.UpgradeFlag(groupID, newClass, flag);
        if (!success)
        {
            string errMsg = "[SimpleGroup] Upgrade T2->T3 failed for group: ";
            errMsg = errMsg + groupID;
            Print(errMsg);
        }
    }
};
