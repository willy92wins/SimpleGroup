// ============================================================================
// LFPG_ActionRegistration.c - 4_World/actions
// Registro global de acciones custom con el engine DayZ.
//
// SIN ESTO, AddAction() en SetActions() falla silenciosamente porque el
// engine no conoce los tipos de accion custom. Las acciones vanilla
// (ActionTogglePlaceObject, ActionDeployObject, ActionTakeItem, etc.)
// ya estan registradas por el engine — solo las custom necesitan esto.
// ============================================================================

modded class ActionConstructor
{
    override void RegisterActions(TTypenameArray actions)
    {
        super.RegisterActions(actions);

        // Territory / Group actions
        actions.Insert(LFPG_ActionRegisterTerritory);
        actions.Insert(LFPG_ActionDestroyFlag);
        actions.Insert(LFPG_ActionInvite);
        actions.Insert(LFPG_ActionJoinGroup);

        // Flag raise / lower
        actions.Insert(LFPG_ActionRaiseFlag);
        actions.Insert(LFPG_ActionLowerFlag);

        // Tier upgrades
        actions.Insert(LFPG_ActionUpgradeT2);
        actions.Insert(LFPG_ActionUpgradeT3);
    }
};
