// ============================================================================
// LFPG_ModdedHologram.c - 4_World/modded
// Restriccion de construccion via modded Hologram
// LFPG_FlagKit_T1: solo bloqueado por territorio ajeno (primer deploy OK)
//
// Routing:
//   GardenPlot + EnablePlots   -> CanPlaceGarden
//   GardenPlot + !EnablePlots  -> CanDeploy (cuenta como mueble)
//   Greenhouse en whitelist    -> CanPlaceGarden
//   Todo lo demas              -> CanDeploy
//
// FIX C-4: LFPG checks ANTES del if (m_IsColliding) para prevalecer sobre
//          colisiones vanilla cuando el motivo LFPG aplica.
// FIX D-15: LFPG_FlagKit_T1 bloquea en territorio ajeno via IsNearOtherTerritory.
// FIX I-5: Auto-resync si el cache esta stale (throttle 5s).
// FIX PLACEMENT: Notifica al jugador el motivo del bloqueo con throttle,
//   y limpia el estado cuando la posicion es valida para permitir re-notificar
//   si el jugador mueve el cursor a una zona invalida.
// ============================================================================

modded class Hologram
{
    // FIX: NO anadir static member vars a modded class - Enforce lo prohibe.
    // El timestamp vive en LFPG_ClientGroupCache.s_HologramResyncMs.

    // Helper: pedir al server datos frescos si el cache parece stale
    protected void LFPG_RequestResyncIfNeeded()
    {
        int nowMs = GetGame().GetTime();
        int diff = nowMs - LFPG_ClientGroupCache.s_HologramResyncMs;
        if (LFPG_ClientGroupCache.s_HologramResyncMs != 0 && diff < 5000)
            return; // throttle

        PlayerBase plr = PlayerBase.Cast(GetGame().GetPlayer());
        if (!plr)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(plr, LFPG_RPC_C2S_REQUEST_GROUP_DATA, true, null);
        LFPG_ClientGroupCache.s_HologramResyncMs = nowMs;
    }

    override void EvaluateCollision(ItemBase action_item = null)
    {
        super.EvaluateCollision(action_item);

        if (!m_Parent)
            return;

        // Cliente only (hologram no existe server-side)
        if (GetGame().IsDedicatedServer())
            return;

        vector projPos = GetProjectionPosition();

        // v3+: Lista B (Unrestricted) — prio total, no tocar colision
        if (LFPG_ClientGroupCache.IsUnrestrictedCached(m_Parent))
        {
            LFPG_ClientGroupCache.NotifyPlacementBlocked(LFPG_BLOCK_NONE);
            return;
        }

        // v3+: Lista A (NoBaseRequired) — ignora colision vanilla,
        // bloquea solo si cae en territorio ajeno (anti-grief)
        if (LFPG_ClientGroupCache.IsNoBaseRequiredCached(m_Parent))
        {
            SetIsColliding(false);
            if (LFPG_ClientGroupCache.IsNearOtherTerritory(projPos))
            {
                SetIsColliding(true);
                LFPG_ClientGroupCache.NotifyPlacementBlocked(LFPG_BLOCK_OTHER_TERRITORY);
            }
            else
            {
                LFPG_ClientGroupCache.NotifyPlacementBlocked(LFPG_BLOCK_NONE);
            }
            return;
        }

        // Otros items: checks LFPG estandar
        // FIX C-4: Si la colision vanilla ya marco true Y uno de los checks LFPG
        // tambien aplica, mantener true (ninguno cancela al otro). Si la colision
        // vanilla es false pero LFPG detecta issue, poner true.

        int blockReason = LFPG_BLOCK_NONE;

        if (!LFPG_ClientGroupCache.HasGroup())
        {
            // Sin grupo: cache puede estar stale, pedir resync
            LFPG_RequestResyncIfNeeded();
            blockReason = LFPG_BLOCK_NO_GROUP;
        }
        else if (!LFPG_ClientGroupCache.IsInBuildZone(projPos))
        {
            blockReason = LFPG_BLOCK_OUTSIDE_ZONE;
        }
        else if (LFPG_ClientGroupCache.IsNearOtherTerritory(projPos))
        {
            // FIX G-2: Aunque este en zona propia, bloquear si cae en zona ajena
            blockReason = LFPG_BLOCK_OTHER_TERRITORY;
        }
        else
        {
            // En zona propia sin conflicto - check limites
            bool useGardenLimit = false;
            string gardenClass = "GardenPlot";
            bool isGarden = m_Parent.IsKindOf(gardenClass);

            if (isGarden && LFPG_ClientGroupCache.s_EnablePlots)
            {
                useGardenLimit = true;
            }
            else if (!isGarden && LFPG_ClientGroupCache.IsGreenhouseCached(m_Parent))
            {
                useGardenLimit = true;
            }

            if (useGardenLimit)
            {
                if (!LFPG_ClientGroupCache.CanPlaceGarden())
                    blockReason = LFPG_BLOCK_GARDEN_LIMIT;
            }
            else
            {
                if (!LFPG_ClientGroupCache.CanDeploy())
                    blockReason = LFPG_BLOCK_DEPLOY_LIMIT;
            }
        }

        if (blockReason != LFPG_BLOCK_NONE)
        {
            SetIsColliding(true);
            LFPG_ClientGroupCache.NotifyPlacementBlocked(blockReason);
        }
        else
        {
            // Posicion valida desde la perspectiva LFPG: limpiar estado de notificacion
            // para permitir re-notificar si el jugador vuelve a una zona invalida.
            LFPG_ClientGroupCache.NotifyPlacementBlocked(LFPG_BLOCK_NONE);
        }
    }
};
