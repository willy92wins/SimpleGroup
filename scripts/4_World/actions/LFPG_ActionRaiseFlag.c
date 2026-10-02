// ============================================================================
// LFPG_ActionRaiseFlag.c - 4_World/actions
// Accion continua: mantener F para subir la bandera
// FIX 3: Client usa Cache (no flag.HasGroup/GetGroupID que no sincronizan)
// ============================================================================

class LFPG_ActionRaiseFlagCB extends ActionContinuousBaseCB
{
    override void CreateActionComponent()
    {
        m_ActionData.m_ActionComponent = new CAContinuousRepeat(1.0);
    }
};

class LFPG_ActionRaiseFlag extends ActionContinuousBase
{
    void LFPG_ActionRaiseFlag()
    {
        m_CallbackClass = LFPG_ActionRaiseFlagCB;
        // CMD_ACTIONFB_INTERACT: funciona con cualquier modelo y con manos vacias (CCINone).
        // CMD_ACTIONFB_CRAFTING requiere item en manos para el animation graph.
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_INTERACT;
        m_FullBody = true;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;

        string text = "#STR_LFPG_ACTION_RAISE_FLAG";
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

    override bool HasProgress()
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

        // Ceder a acciones de upgrade SOLO cuando la upgrade realmente pasaria.
        // Si el jugador tiene la herramienta pero faltan materiales en los slots,
        // NO ceder — mostrar RaiseFlag en su lugar.
        EntityAI handsEntity = player.GetHumanInventory().GetEntityInHands();
        if (handsEntity)
        {
            // FIX I-8: via ToolMatcher para aceptar variantes mod
            if (LFPG_IsSledgeHammer(handsEntity) && flag.GetTier() == 1)
            {
                string slotLog = "LFPG_FlagLog";
                string slotRope = "LFPG_FlagRope";
                if (flag.FindAttachmentBySlotName(slotLog) && flag.FindAttachmentBySlotName(slotRope))
                    return false;
            }
            if (LFPG_IsPickaxe(handsEntity) && flag.GetTier() == 2)
            {
                string slotFW = "LFPG_FlagFirewood";
                string slotNails = "LFPG_FlagNails";
                string slotStones = "LFPG_FlagStones";
                EntityAI fwAtt = flag.FindAttachmentBySlotName(slotFW);
                EntityAI nailsAtt = flag.FindAttachmentBySlotName(slotNails);
                EntityAI stonesAtt = flag.FindAttachmentBySlotName(slotStones);
                if (fwAtt && nailsAtt && stonesAtt)
                {
                    ItemBase fwItem = ItemBase.Cast(fwAtt);
                    ItemBase nailsItem = ItemBase.Cast(nailsAtt);
                    ItemBase stonesItem = ItemBase.Cast(stonesAtt);
                    if (fwItem && fwItem.GetQuantity() >= 6 && nailsItem && nailsItem.GetQuantity() >= 60 && stonesItem && stonesItem.GetQuantity() >= 10)
                        return false;
                }
            }
        }

        // Bandera no completamente subida (SyncVar, disponible ambos lados)
        if (flag.m_RaiseProgressNet >= 1.0)
            return false;

        // Acciones de bandera deshabilitadas para este tier
        if (!flag.m_FlagActionsEnabledNet)
            return false;

        return true;
    }

    override void OnFinishProgressServer(ActionData action_data)
    {
        LFPG_FlagBase flag = LFPG_FlagBase.Cast(action_data.m_Target.GetObject());
        if (!flag)
            return;

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        LFPG_TerritoryConfig config = mgr.GetConfig();
        if (!config)
            return;

        float delta = config.m_FlagRaiseRatePerSecond;
        flag.IncrementRaiseProgress(delta);

        float progress = flag.ComputeCurrentRaiseProgress();
        if (progress >= 1.0)
        {
            flag.SetFullyRaised();
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
