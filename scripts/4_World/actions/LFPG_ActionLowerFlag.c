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
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONFB_LOWER_FLAG;
        m_FullBody = true;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT;

        string text = "#STR_LFPG_ACTION_LOWER_FLAG";
        m_Text = text;
    }

    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINone;
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

        // FIX 3: Client-side usa SOLO el cache
        if (!GetGame().IsDedicatedServer())
        {
            if (!LFPG_ClientGroupCache.HasGroup())
                return false;

            if (!LFPG_ClientGroupCache.IsFlagAtPosition(flag.GetPosition()))
                return false;

            if (flag.m_RaiseProgressNet <= 0.0)
                return false;

            return true;
        }

        // Server-side
        if (!flag.HasGroup())
            return false;

        float progress = flag.ComputeCurrentRaiseProgress();
        if (progress <= 0.0)
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

        float delta = config.m_FlagLowerRatePerSecond;
        flag.DecrementRaiseProgress(delta);
    }
};
