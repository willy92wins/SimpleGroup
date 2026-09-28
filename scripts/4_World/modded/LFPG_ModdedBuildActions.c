// ============================================================================
// LFPG_ModdedBuildActions.c - 4_World/modded
// Territory gate for building on an EXISTING base, and for the vanilla routes that
// alter or pack that base without going through ActionBuildPart / ActionDismantlePart.
//
// ActionBuildPart and ActionDismantlePart never reach CanBePlaced nor
// OnPlacementComplete (actionbuildpart.c:89-129, actiondismantlepart.c:78-90), so
// LFPG_ModdedBaseBuildingBase leaves both ungated: an outsider could extend or take
// apart a base standing inside someone else's territory.
//
// The same owner check covers ActionFoldBaseBuildingObject, ActionBuildShelter,
// ActionCreateGreenhouseGardenPlot, ActionDeconstructShelter, ActionPackTent,
// ActionDismantleGardenPlot and ActionFoldObject. Those actions target an existing
// object and never call the build/dismantle gate.
//
// ActionPackTent aims at the tent "pack" proxy. Vanilla packs the parent
// (actionpacktent.c:134), so the gate uses GetParent() when it is set.
// ActionCreateGreenhouseGardenPlot skips the gate when the plot type it creates
// is on the unrestricted list. No other action here honours that list.
//
// ActionDestroyPart stays deliberately free: bringing a base down is intended raid
// play (ADR-2026-08-15-B).
// ============================================================================

class LFPG_BuildGate
{
    // Server-authoritative. LFPG_GroupManager.Get() is null on the client, so the
    // client half falls through to the cache check in ActionCondition below.
    // One rule for every gated action: outside a zone it is allowed; inside a zone
    // only a member of the owner group passes.
    static bool AllowsPartFor(PlayerBase player, Object targetObj)
    {
        if (!targetObj)
            return true;

        // Fail-closed on the server: a missing manager there means the gate cannot be
        // evaluated, and letting the action through would reopen the hole this class
        // closes. On the client the singleton is legitimately null, so the check falls
        // through to ActionCondition.
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return !GetGame().IsDedicatedServer();

        // Not inside any territory: nothing to protect.
        string ownerGroup = mgr.FindGroupIDAtPosition(targetObj.GetPosition());
        if (ownerGroup == "")
            return true;

        if (!player)
            return false;

        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return false;

        // A player with no group gets "" here, which never matches an owner group.
        return mgr.GetPlayerGroupID(identity.GetPlainId()) == ownerGroup;
    }

    static bool AllowsPart(ActionData action_data)
    {
        if (!action_data)
            return true;
        if (!action_data.m_Target)
            return true;

        Object targetObj = action_data.m_Target.GetObject();
        PlayerBase pb = PlayerBase.Cast(action_data.m_Player);
        return AllowsPartFor(pb, targetObj);
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

        return AllowsPartClientObject(target.GetObject());
    }

    // Same client rule as AllowsPartClient, for an object that is not target.GetObject().
    static bool AllowsPartClientObject(Object targetObj)
    {
        if (!targetObj)
            return true;

        vector targetPos = targetObj.GetPosition();

        // Inside our own build zone the server resolves the nearest owner as us.
        if (LFPG_ClientGroupCache.IsInBuildZone(targetPos))
            return true;

        return !LFPG_ClientGroupCache.IsNearOtherTerritory(targetPos);
    }

    // ActionPackTent's target object is the pack proxy. The tent that gets packed
    // is the parent (actionpacktent.c:134, rebuilt via GetBoneObject in actionbase.c:554).
    static Object PackTentObject(ActionTarget target)
    {
        if (!target)
            return null;

        Object parentObj = target.GetParent();
        if (parentObj)
            return parentObj;

        return target.GetObject();
    }

    // The plot does not exist yet. Match the classname vanilla will spawn
    // (actioncreategreenhousegardenplot.c:102 and :106) against list B the same
    // way LFPG_TerritoryConfig.IsUnrestricted walks m_UnrestrictedTypes.
    static bool GreenhouseCreateUnrestricted(ActionTarget target)
    {
        string plotType = "GardenPlotGreenhouse";
        if (target)
        {
            Land_Misc_Polytunnel tunnel = Land_Misc_Polytunnel.Cast(target.GetObject());
            if (tunnel)
                plotType = "GardenPlotPolytunnel";
        }

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
            return PlotTypeInConfig(mgr.GetConfig(), plotType);

        return PlotTypeInClientCache(plotType);
    }

    static bool PlotTypeInConfig(LFPG_TerritoryConfig cfg, string plotType)
    {
        if (!cfg)
            return false;
        if (!cfg.m_UnrestrictedTypes)
            return false;

        int countSrv = cfg.m_UnrestrictedTypes.Count();
        int iSrv;
        for (iSrv = 0; iSrv < countSrv; iSrv = iSrv + 1)
        {
            string listedSrv = cfg.m_UnrestrictedTypes[iSrv];
            if (listedSrv == plotType)
                return true;

            // IsKindOf lowercases its parent argument (game.c:1438). Pass a copy.
            string listedCopy = listedSrv + "";
            if (GetGame().IsKindOf(plotType, listedCopy))
                return true;
        }
        return false;
    }

    static bool PlotTypeInClientCache(string plotType)
    {
        if (!LFPG_ClientGroupCache.s_UnrestrictedTypes)
            return false;

        int countCli = LFPG_ClientGroupCache.s_UnrestrictedTypes.Count();
        int iCli;
        for (iCli = 0; iCli < countCli; iCli = iCli + 1)
        {
            string listedCli = LFPG_ClientGroupCache.s_UnrestrictedTypes[iCli];
            if (listedCli == plotType)
                return true;

            string listedCliCopy = listedCli + "";
            if (GetGame().IsKindOf(plotType, listedCliCopy))
                return true;
        }
        return false;
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

modded class ActionFoldBaseBuildingObject
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

modded class ActionBuildShelter
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

modded class ActionCreateGreenhouseGardenPlot
{
    // No client-side check here: whether the created plot type is unrestricted comes from the
    // server config, which a player without a group never receives. The action stays offered
    // and the server decides in ActionConditionContinue.
    override bool ActionConditionContinue(ActionData action_data)
    {
        if (!super.ActionConditionContinue(action_data))
            return false;

        ActionTarget plotTarget = null;
        if (action_data)
            plotTarget = action_data.m_Target;
        if (LFPG_BuildGate.GreenhouseCreateUnrestricted(plotTarget))
            return true;

        return LFPG_BuildGate.AllowsPart(action_data);
    }
};

modded class ActionDeconstructShelter
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

modded class ActionPackTent
{
    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!super.ActionCondition(player, target, item))
            return false;

        if (GetGame().IsDedicatedServer())
            return true;

        return LFPG_BuildGate.AllowsPartClientObject(LFPG_BuildGate.PackTentObject(target));
    }

    override bool ActionConditionContinue(ActionData action_data)
    {
        if (!super.ActionConditionContinue(action_data))
            return false;

        if (!action_data)
            return true;

        ActionTarget packTarget = action_data.m_Target;
        PlayerBase packPlayer = PlayerBase.Cast(action_data.m_Player);
        return LFPG_BuildGate.AllowsPartFor(packPlayer, LFPG_BuildGate.PackTentObject(packTarget));
    }
};

modded class ActionDismantleGardenPlot
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

modded class ActionFoldObject
{
    // Interact actions have no continue check. The dedicated server re-evaluates
    // ActionCondition when the action starts.
    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!super.ActionCondition(player, target, item))
            return false;

        if (GetGame().IsDedicatedServer())
        {
            Object foldTarget = null;
            if (target)
                foldTarget = target.GetObject();
            return LFPG_BuildGate.AllowsPartFor(player, foldTarget);
        }

        return LFPG_BuildGate.AllowsPartClient(target);
    }
};
