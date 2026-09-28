// ============================================================================
// LFPG_ModdedActionDeploy.c - 4_World/modded
//
// Universal deploy-action gate para enforcement de territorio.
//
// Target: `ActionDeployObject` (base vanilla). Por herencia esto captura:
//   - ActionDeployObject           (vanilla, kits simples)
//   - ActionPlaceObject            (vanilla: ActionPlaceObject extends ActionDeployObject)
//   - LFPG_ActionPlaceGeneric      (LFPowerGrid: extends ActionPlaceObject)
//   - LFPG_ActionPlaceLogicGate    (LFPowerGrid: extends LFPG_ActionPlaceGeneric)
//   - Cualquier mod que herede por la misma rama
//
// Por que aqui y no en Hologram:
//   Vanilla ActionDeployObject.ActionCondition ya consulta Hologram.IsColliding(),
//   pero mods tipo "Build Everywhere" override SetIsColliding(false) → nuestra
//   marca de bloqueo se pierde. Aqui chequeamos LFPG DIRECTAMENTE, independiente
//   del estado de colision del hologram. Nadie puede bypasear esto sin modar
//   explicitamente el ActionCondition de vanilla.
//
// Capas:
//   1. ActionCondition (client)       → click no dispara la action si LFPG bloquea
//   2. ActionConditionContinue (srv)  → action aborta mid-progress si server ve estado invalido
//
// Resultado: nunca se llega a OnFinishProgressServer → nunca se llama
// item.OnPlacementComplete() → el CreateObjectEx del kit nunca ejecuta.
// No hace falta modar LFPG_KitBase ni ninguna clase mod-especifica.
//
// Reglas basadas en dos whitelists configurables (ver LFPG_TerritoryConfig v3+):
//   - Lista A (NoBaseRequired): placeable sin grupo/zona propia, pero bloqueado
//     en territorio ajeno. Default incluye LFPG_FlagKit_T1.
//   - Lista B (Unrestricted): placeable en cualquier parte, incluyendo ajeno.
//     Vacia por defecto.
// Prioridad: Lista B gana si el item esta en ambas.
// ============================================================================

modded class ActionDeployObject
{
    // ------------------------------------------------------------------------
    // CLIENT: click no dispara la action si LFPG bloquea
    // ------------------------------------------------------------------------
    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        if (!g_Game.IsDedicatedServer())
        {
            if (player && player.IsPlacingLocal() && item)
            {
                Hologram holoLocal = player.GetHologramLocal();
                if (holoLocal)
                {
                    vector pos = holoLocal.GetProjectionPosition();
                    if (LFPG_IsDeployBlockedClient(item, pos))
                        return false;
                }
            }
        }
        return super.ActionCondition(player, target, item);
    }

    // ------------------------------------------------------------------------
    // SERVER: action aborta mid-progress si LFPG bloquea
    // Anti-cheat: aunque el cliente bypasee ActionCondition, server re-valida.
    // ------------------------------------------------------------------------
    override bool ActionConditionContinue(ActionData action_data)
    {
        if (g_Game.IsDedicatedServer())
        {
            if (action_data && action_data.m_Player && action_data.m_Player.IsPlacingServer() && action_data.m_MainItem)
            {
                Hologram holoServer = action_data.m_Player.GetHologramServer();
                if (holoServer)
                {
                    vector pos = holoServer.GetProjectionPosition();
                    if (LFPG_IsDeployBlockedServer(action_data.m_Player, action_data.m_MainItem, pos))
                        return false;
                }
            }
        }
        return super.ActionConditionContinue(action_data);
    }
}

// ============================================================================
// Helpers de chequeo LFPG — fuera de la modded class para poder reusarlos
// si hace falta (Hologram, otros puntos futuros).
// ============================================================================

// Devuelve true si la deploy debe bloquearse. Client-side: usa el cache.
bool LFPG_IsDeployBlockedClient(ItemBase item, vector pos)
{
    if (!item)
        return false;

    // Lista B: sin restriccion alguna (prioridad sobre lista A)
    if (LFPG_ClientGroupCache.IsUnrestrictedCached(item))
        return false;

    // Lista A: placement sin grupo/zona propia, pero bloqueado en ajena
    if (LFPG_ClientGroupCache.IsNoBaseRequiredCached(item))
    {
        // Solo bloquea si cae dentro de territorio ajeno
        if (LFPG_ClientGroupCache.HasGroup())
            return LFPG_ClientGroupCache.IsNearOtherTerritory(pos);
        // Sin grupo: comparar contra cualquier territorio (seria "ajeno" por defecto)
        return LFPG_ClientGroupCache.IsNearOtherTerritory(pos);
    }

    // Regla estandar: requiere grupo + zona propia + territorio ajeno no + limite OK
    if (!LFPG_ClientGroupCache.HasGroup())
        return true;

    if (!LFPG_ClientGroupCache.IsInBuildZone(pos))
        return true;

    if (LFPG_ClientGroupCache.IsNearOtherTerritory(pos))
        return true;

    if (!LFPG_ClientGroupCache.CanDeploy())
        return true;

    return false;
}

// Devuelve true si la deploy debe bloquearse. Server-side: usa el GroupManager.
bool LFPG_IsDeployBlockedServer(PlayerBase player, ItemBase item, vector pos)
{
    if (!player || !item)
        return false;

    LFPG_GroupManager mgr = LFPG_GroupManager.Get();
    if (!mgr)
        return false;

    LFPG_TerritoryConfig cfg = mgr.GetConfig();
    if (!cfg)
        return false;

    // No-drop blacklist wins over list B and list A, including a type on both lists.
    if (LFPG_IsListedDropBlocked(item, player, pos))
        return true;

    // Lista B: sin restriccion alguna (prioridad sobre lista A)
    if (cfg.IsUnrestricted(item))
        return false;

    PlayerIdentity identity = player.GetIdentity();
    if (!identity)
        return false;

    string playerUID = identity.GetPlainId();
    string groupID = mgr.GetPlayerGroupID(playerUID);

    // Lista A: placement sin grupo/zona propia, pero bloqueado en ajena
    if (cfg.IsNoBaseRequired(item))
    {
        if (groupID != "")
            return mgr.IsPositionInOtherTerritory(pos, groupID);
        // Sin grupo: cualquier territorio existente es ajeno
        return mgr.IsPositionInTerritory(pos);
    }

    // Regla estandar
    if (groupID == "")
        return true;

    if (!mgr.IsInBuildZone(playerUID, pos))
        return true;

    if (mgr.IsPositionInOtherTerritory(pos, groupID))
        return true;

    if (!mgr.CanDeploy(groupID))
        return true;

    return false;
}
