// ============================================================================
// LFPG_ActionRaiseFlag.c — 4_World/actions
// Acción continua: mantener F para subir la bandera
// Patrón vanilla: CAContinuousRepeat(1) — ejecuta cada ~1 segundo
// Condición: miembro del grupo + bandera no completamente subida
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
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_RAISE_FLAG;
        m_FullBody = true;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT;

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

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!player || !target)
            return false;

        LFPG_FlagBase flag = LFPG_FlagBase.Cast(target.GetObject());
        if (!flag)
            return false;

        // Solo si la bandera tiene grupo asignado
        if (!flag.HasGroup())
            return false;

        // Solo si la bandera no está completamente subida
        float progress = flag.ComputeCurrentRaiseProgress();
        if (progress >= 1.0)
            return false;

        // Solo miembros del grupo
        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return false;

        string playerUID = identity.GetPlainId();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();

        // Client-side: usar cache
        if (!IsDedicatedServer())
        {
            if (!LFPG_ClientGroupCache.HasGroup())
                return false;
            if (LFPG_ClientGroupCache.s_GroupID != flag.GetGroupID())
                return false;
            return true;
        }

        // Server-side: verificar membership
        if (mgr)
        {
            string groupID = mgr.GetPlayerGroupID(playerUID);
            if (groupID == "" || groupID != flag.GetGroupID())
                return false;
        }

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

        // Si llegó a 1.0, la bandera está completamente subida
        float progress = flag.ComputeCurrentRaiseProgress();
        if (progress >= 1.0)
        {
            flag.SetFullyRaised();
        }
    }
};
