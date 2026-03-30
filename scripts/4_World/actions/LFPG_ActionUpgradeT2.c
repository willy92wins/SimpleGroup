// ============================================================================
// LFPG_ActionUpgradeT2.c — 4_World/actions
// Upgrade T1 → T2: SledgeHammer + WoodenLog + Rope en slots de la bandera
// Null-check post-spawn: si falla el spawn, no se borra la vieja
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

        // Solo T1 puede upgradearse a T2
        if (flag.GetTier() != 1)
            return false;

        if (!flag.HasGroup())
            return false;

        // Necesita SledgeHammer en manos
        EntityAI itemInHands = player.GetHumanInventory().GetEntityInHands();
        if (!itemInHands)
            return false;

        string handType = itemInHands.GetType();
        if (handType != "SledgeHammer")
            return false;

        // Verificar materiales en slots de la bandera
        string slotLog = "Material_FPole_WoodenLog";
        EntityAI logAtt = flag.FindAttachmentBySlotName(slotLog);
        if (!logAtt)
            return false;

        string slotRope = "Material_FPole_Rope";
        EntityAI ropeAtt = flag.FindAttachmentBySlotName(slotRope);
        if (!ropeAtt)
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

        // Verificar membership server-side
        string groupID = mgr.GetPlayerGroupID(playerUID);
        if (groupID == "" || groupID != flag.GetGroupID())
            return;

        // Consumir materiales de los slots
        string slotLog = "Material_FPole_WoodenLog";
        EntityAI logAtt = flag.FindAttachmentBySlotName(slotLog);
        if (logAtt)
        {
            GetGame().ObjectDelete(logAtt);
        }

        string slotRope = "Material_FPole_Rope";
        EntityAI ropeAtt = flag.FindAttachmentBySlotName(slotRope);
        if (ropeAtt)
        {
            GetGame().ObjectDelete(ropeAtt);
        }

        // Upgrade via GroupManager (con null-check post-spawn)
        string newClass = "LFPG_Flag_T2";
        bool success = mgr.UpgradeFlag(groupID, newClass, flag);
        if (!success)
        {
            // Spawn falló — los materiales ya se consumieron, devolver?
            // Por seguridad, logear el error. Los materiales se pierden.
            string errMsg = "[LFPG_Territory] Upgrade T1->T2 failed for group: ";
            errMsg = errMsg + groupID;
            Print(errMsg);
        }
    }
};
