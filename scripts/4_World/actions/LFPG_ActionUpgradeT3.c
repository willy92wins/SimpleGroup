// ============================================================================
// LFPG_ActionUpgradeT3.c — 4_World/actions
// Upgrade T2 → T3: Pickaxe + 6 Firewood + 60 Nails + 16 Stone en slots
// Los stackMax vanilla son exactos: Firewood=6, Nails=60, Stones=16
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

        // Solo T2 puede upgradearse a T3
        if (flag.GetTier() != 2)
            return false;

        if (!flag.HasGroup())
            return false;

        // Necesita Pickaxe en manos
        EntityAI itemInHands = player.GetHumanInventory().GetEntityInHands();
        if (!itemInHands)
            return false;

        string handType = itemInHands.GetType();
        if (handType != "Pickaxe")
            return false;

        // Verificar materiales en slots: stackMax exactos
        // Firewood: 6 unidades en slot "Firewood"
        string slotFW = "Firewood";
        EntityAI fwAtt = flag.FindAttachmentBySlotName(slotFW);
        if (!fwAtt)
            return false;
        ItemBase fwItem = ItemBase.Cast(fwAtt);
        if (!fwItem || fwItem.GetQuantity() < 6)
            return false;

        // Nails: 60 unidades en slot "Material_FPole_Nails"
        string slotNails = "Material_FPole_Nails";
        EntityAI nailsAtt = flag.FindAttachmentBySlotName(slotNails);
        if (!nailsAtt)
            return false;
        ItemBase nailsItem = ItemBase.Cast(nailsAtt);
        if (!nailsItem || nailsItem.GetQuantity() < 60)
            return false;

        // Stones: 16 unidades en slot "Stones"
        string slotStones = "Stones";
        EntityAI stonesAtt = flag.FindAttachmentBySlotName(slotStones);
        if (!stonesAtt)
            return false;
        ItemBase stonesItem = ItemBase.Cast(stonesAtt);
        if (!stonesItem || stonesItem.GetQuantity() < 16)
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

        string playerUID = identity.GetPlainId();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        string groupID = mgr.GetPlayerGroupID(playerUID);
        if (groupID == "" || groupID != flag.GetGroupID())
            return;

        // Doble check de materiales y cantidades server-side
        string slotFW = "Firewood";
        EntityAI fwAtt = flag.FindAttachmentBySlotName(slotFW);
        if (!fwAtt)
            return;
        ItemBase fwItem = ItemBase.Cast(fwAtt);
        if (!fwItem || fwItem.GetQuantity() < 6)
            return;

        string slotNails = "Material_FPole_Nails";
        EntityAI nailsAtt = flag.FindAttachmentBySlotName(slotNails);
        if (!nailsAtt)
            return;
        ItemBase nailsItem = ItemBase.Cast(nailsAtt);
        if (!nailsItem || nailsItem.GetQuantity() < 60)
            return;

        string slotStones = "Stones";
        EntityAI stonesAtt = flag.FindAttachmentBySlotName(slotStones);
        if (!stonesAtt)
            return;
        ItemBase stonesItem = ItemBase.Cast(stonesAtt);
        if (!stonesItem || stonesItem.GetQuantity() < 16)
            return;

        // Upgrade PRIMERO — attachments se destruyen con la bandera vieja
        string newClass = "LFPG_Flag_T3";
        bool success = mgr.UpgradeFlag(groupID, newClass, flag);
        if (!success)
        {
            string errMsg = "[LFPG_Territory] Upgrade T2->T3 failed for group: ";
            errMsg = errMsg + groupID;
            Print(errMsg);
        }
    }
};
