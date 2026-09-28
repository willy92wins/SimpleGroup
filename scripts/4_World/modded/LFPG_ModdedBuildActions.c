// ============================================================================
// LFPG_ModdedBuildActions.c - 4_World/modded
// Territory gate for building on an EXISTING base.
//
// ActionBuildPart and ActionDismantlePart never reach CanBePlaced nor
// OnPlacementComplete (actionbuildpart.c:89-129, actiondismantlepart.c:78-90), so
// LFPG_ModdedBaseBuildingBase leaves both ungated: an outsider could extend or take
// apart a base standing inside someone else's territory.
//
// ActionDestroyPart stays deliberately free: bringing a base down is intended raid
// play (ADR-2026-08-15-B).
// ============================================================================

class LFPG_BuildGate
{
    // Server-authoritative. LFPG_GroupManager.Get() is null on the client, so the
    // client half falls through to the cache check in ActionCondition below.
    static bool AllowsPart(ActionData action_data)
    {
        if (!action_data)
            return true;
        if (!action_data.m_Target)
            return true;

        Object targetObj = action_data.m_Target.GetObject();
        if (!targetObj)
            return true;

        // Fail-closed on the server: a missing manager there means the gate cannot be
        // evaluated, and letting the build through would reopen the very hole this class
        // closes. On the client the singleton is legitimately null, so the check falls
        // through to ActionCondition.
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return !GetGame().IsDedicatedServer();

        // Not inside any territory: nothing to protect.
        string ownerGroup = mgr.FindGroupIDAtPosition(targetObj.GetPosition());
        if (ownerGroup == "")
            return true;

        PlayerBase pb = PlayerBase.Cast(action_data.m_Player);
        if (!pb)
            return false;

        PlayerIdentity identity = pb.GetIdentity();
        if (!identity)
            return false;

        // A player with no group gets "" here, which never matches an owner group.
        return mgr.GetPlayerGroupID(identity.GetPlainId()) == ownerGroup;
    }

    // Client-side predicate so the action does not offer itself and then abort mid-way.
    //
    // Deliberately more permissive than the server: it may show an action the server then
    // refuses, but it must never hide one the server would allow. IsNearOtherTerritory
    // alone was not enough — it skips only the player's OWN flag
    // (LFPG_ClientGroupCache.c:243-244), so a foreign raised flag within the build radius
    // hid Build/Dismantle from the legitimate owner standing on his own base. That happens
    // with sequential settlement: a neighbour plants at 30.1 m, which is legal because the
    // territory test is strict, and the older base falls inside his radius.
    static bool AllowsPartClient(ActionTarget target)
    {
        if (!target)
            return true;

        Object targetObj = target.GetObject();
        if (!targetObj)
            return true;

        vector targetPos = targetObj.GetPosition();

        // Inside our own build zone the server resolves the nearest owner as us.
        if (LFPG_ClientGroupCache.IsInBuildZone(targetPos))
            return true;

        return !LFPG_ClientGroupCache.IsNearOtherTerritory(targetPos);
    }
};

modded class ActionBuildPart
{
    // Also covers ActionBuildPartNoTool, which inherits from this class.
    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!super.ActionCondition(player, target, item))
            return false;

        if (GetGame().IsDedicatedServer())
            return true;

        return LFPG_BuildGate.AllowsPartClient(target);
    }

    override bool ActionConditionContinue(ActionData action_data)
    {
        if (!super.ActionConditionContinue(action_data))
            return false;

        return LFPG_BuildGate.AllowsPart(action_data);
    }
};

modded class ActionDismantlePart
{
    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!super.ActionCondition(player, target, item))
            return false;

        if (GetGame().IsDedicatedServer())
            return true;

        return LFPG_BuildGate.AllowsPartClient(target);
    }

    override bool ActionConditionContinue(ActionData action_data)
    {
        if (!super.ActionConditionContinue(action_data))
            return false;

        return LFPG_BuildGate.AllowsPart(action_data);
    }
};
