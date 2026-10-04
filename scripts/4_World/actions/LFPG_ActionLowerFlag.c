// ============================================================================
// LFPG_ActionLowerFlag.c - 4_World/actions
// Accion continua: mantener F para bajar la bandera
// FIX 3: Client usa Cache
// ============================================================================

class LFPG_ActionLowerFlagCB extends ActionContinuousBaseCB
{
    override void CreateActionComponent()
    {
        m_ActionData.m_ActionComponent = new CAContinuousRepeat(1.0);
    }
};

class LFPG_ActionLowerFlag extends ActionContinuousBase
{
    void LFPG_ActionLowerFlag()
    {
        m_CallbackClass = LFPG_ActionLowerFlagCB;
        // CMD_ACTIONFB_INTERACT: funciona con cualquier modelo y con manos vacias (CCINone).
        // CMD_ACTIONFB_CRAFTING requiere item en manos para el animation graph.
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_INTERACT;
        m_FullBody = true;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;

        string text = "#STR_LFPG_ACTION_LOWER_FLAG";
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
        // NO ceder — mostrar LowerFlag en su lugar.
        EntityAI handsEntity = player.GetHumanInventory().GetEntityInHands();
        if (handsEntity)
        {
            // FIX I-8: via ToolMatcher para aceptar variantes mod
            if (LFPG_IsSledgeHammer(handsEntity) && flag.GetTier() == 1 && LFPG_ActionGuards.IsPlayerInFlagGroup(player, flag))
            {
                string slotLog = "LFPG_FlagLog";
                string slotRope = "LFPG_FlagRope";
                if (flag.FindAttachmentBySlotName(slotLog) && flag.FindAttachmentBySlotName(slotRope))
                    return false;
            }
            if (LFPG_IsPickaxe(handsEntity) && flag.GetTier() == 2 && LFPG_ActionGuards.IsPlayerInFlagGroup(player, flag))
            {
                if (LFPG_ActionGuards.HasT3UpgradeMaterials(flag))
                    return false;
            }
        }

        // Bandera no completamente bajada (SyncVar, disponible ambos lados)
        if (flag.m_RaiseProgressNet <= 0.0)
            return false;

        // No se puede bajar mientras tenga energia (SyncVar)
        if (flag.m_IsPoweredNet)
            return false;

        // Acciones de bandera deshabilitadas para este tier (SyncVar)
        if (!flag.m_FlagActionsEnabledNet)
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

        LFPG_FlagBase flag = LFPG_FlagBase.Cast(action_data.m_Target.GetObject());
        if (!flag)
            return;

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        LFPG_TerritoryConfig config = mgr.GetConfig();
        if (!config)
            return;

        float delta = config.m_FlagLowerRatePerSecond;
        flag.DecrementRaiseProgress(delta);
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
