// ============================================================================
// LFPG_ActionInvite.c - 4_World/actions
// Accion instantanea: pulsar F para activar modo invitacion
// ActionInteractBase (F press) - NO ActionContinuousBase (Hold F)
// Motivo: ActionContinuousBase colisiona con LowerFlag/RaiseFlag en el mismo
// slot de input (Hold F). DayZ muestra solo la primera que pase condiciones,
// ocultando Invite detras de LowerFlag. Con ActionInteractBase, ambas
// aparecen simultaneamente: [F] Invite + [Hold F] Lower Flag.
// Client usa Cache
// ============================================================================

class LFPG_ActionInvite extends ActionInteractBase
{
    void LFPG_ActionInvite()
    {
        string text = "#STR_LFPG_ACTION_INVITE";
        m_Text = text;
    }

    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINone;
        m_ConditionTarget = new CCTCursor(5.0);
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

        // Invite mode ya activo (SyncVar, funciona en ambos lados)
        if (flag.IsInviteModeActive())
            return false;

        // Client-side usa SOLO el cache
        if (!GetGame().IsDedicatedServer())
        {
            if (!LFPG_ClientGroupCache.HasGroup())
                return false;

            if (!LFPG_ClientGroupCache.IsFlagAtPosition(flag.GetPosition()))
                return false;

            // Bandera debe estar subida
            if (flag.m_RaiseProgressNet <= 0.0)
                return false;

            return true;
        }

        // Server-side
        if (!LFPG_ActionGuards.IsPlayerNearTarget(player, targetObj))
            return false;

        if (!flag.HasGroup())
            return false;

        if (flag.IsFullyLowered())
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

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        LFPG_TerritoryConfig config = mgr.GetConfig();
        if (!config)
            return;

        // FIX G-15: Re-validar server-side que la flag pertenece al grupo del sender
        // (defensivo - ActionCondition client-side podria estar desincronizado)
        string playerUID = identity.GetPlainId();
        string senderGroup = mgr.GetPlayerGroupID(playerUID);
        if (senderGroup == "" || flag.GetGroupID() != senderGroup)
        {
            mgr.SendErrorToPlayer(identity, player, "#STR_LFPG_ERR_JOIN_FAILED");
            return;
        }

        // No activar invite si el grupo esta lleno
        LFPG_GroupData group = mgr.GetGroupByPlayer(playerUID);
        if (group && group.GetMemberCount() >= config.m_MaxGroupSize)
        {
            mgr.SendErrorToPlayer(identity, player, "#STR_LFPG_ERR_GROUP_FULL");
            return;
        }

        int durationMs = config.m_InviteDurationSeconds * 1000;
        flag.ActivateInviteMode(durationMs);
    }
};
