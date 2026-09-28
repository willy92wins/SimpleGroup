// ============================================================================
// LFPG_ModdedItemBase.c - 4_World/modded
// Validacion server-side y conteo para items NO-BaseBuildingBase
//
// FIX D: OnPlacementComplete anti-cheat + conteo
// FIX E: EEItemLocationChanged restriccion de drops
// FIX G-1: Kits con IsBasebuildingKit no incrementan counter (evita leak)
// FIX C-3: Idempotencia via IsTracked/IsGardenTracked
// FIX G-2: Bloqueo de territorio ajeno
// FIX D-13: Skip cuando items caen de vehiculos destruidos
// FIX I-4: Guard explicito para oldLocType UNKNOWN (spawn inicial)
// FIX PLACEMENT:
//   - OnPlacementComplete server envia error especifico via LFPG_GetBlockReasonMsg
//   - Drop flow detecta territorio ajeno (nuevo) y envia error diferenciado
//     (no_group / outside_zone / other_territory / deploy_limit / garden_limit)
// ============================================================================

modded class ItemBase
{
    // Helper: combina filtro de exclusion (3_Game) + config whitelist + IsDeployable (4_World)
    protected bool LFPG_IsFurniture()
    {
        if (LFPG_IsExcludedFromDeploy(this))
            return false;

        // Exclusion configurable (BatteryCharger, Fireplace, ExpansionMarket, etc.)
        LFPG_GroupManager mgrFurn = LFPG_GroupManager.Get();
        if (mgrFurn)
        {
            LFPG_TerritoryConfig cfgFurn = mgrFurn.GetConfig();
            if (cfgFurn && cfgFurn.IsTypeExcludedFromFurniture(this))
                return false;
        }

        if (IsInherited(BaseBuildingBase))
            return true;

        if (IsDeployable())
            return true;

        return false;
    }

    // Helper: determina si este item debe contar como plot (garden) en vez de mueble
    // Requiere: EnableGreenhouseAsPlot=true, EnablePlots=true, item en GreenhouseWhitelist
    protected bool LFPG_IsGreenhousePlot()
    {
        LFPG_GroupManager mgrGH = LFPG_GroupManager.Get();
        if (!mgrGH)
            return false;

        LFPG_TerritoryConfig cfgGH = mgrGH.GetConfig();
        if (!cfgGH)
            return false;

        return cfgGH.IsGreenhouse(this);
    }

    // Validacion server-side y conteo para items no-BBB
    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);

        #ifdef SERVER
        if (IsInherited(BaseBuildingBase))
            return;
        if (IsInherited(GardenPlot))
            return;

        string flagBaseClass = "LFPG_FlagBase";
        if (IsKindOf(flagBaseClass))
            return;

        // v3+: items en Lista A o B gestionan su placement fuera de este flujo
        // (Lista A = reglas propias via CanBePlaced; Lista B = sin restriccion).
        // Early-return para no contar ni validar como mueble.
        LFPG_GroupManager mgrCfg = LFPG_GroupManager.Get();
        if (mgrCfg)
        {
            LFPG_TerritoryConfig cfgEarly = mgrCfg.GetConfig();
            if (cfgEarly && (cfgEarly.IsNoBaseRequired(this) || cfgEarly.IsUnrestricted(this)))
                return;
        }

        // Basebuilding kits do not count as furniture: what occupies a slot is the
        // structure they spawn. The note that used to sit here claimed
        // ActionDeployObject.OnEndServer makes that structure run its own
        // OnPlacementComplete. It does not: actiondeployobject.c:230-233 only deletes the
        // kit, and fencekit.c:27-29 spawns the Fence with CreateObjectEx. LFPG_ModdedKits
        // reconciles the count instead.
        if (IsBasebuildingKit())
            return;

        if (!LFPG_IsFurniture())
            return;

        PlayerBase pb = PlayerBase.Cast(player);
        if (!pb)
            return;

        PlayerIdentity identity = pb.GetIdentity();
        if (!identity)
            return;

        string playerUID = identity.GetPlainId();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        string groupID = mgr.GetPlayerGroupID(playerUID);

        if (groupID == "")
        {
            LFPG_Log.Error("Unauthorized ItemBase build by " + playerUID + " - no group. Deleting.");
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_NO_GROUP));
            GetGame().ObjectDelete(this);
            return;
        }

        if (!mgr.IsInBuildZone(playerUID, position))
        {
            LFPG_Log.Error("ItemBase build outside zone by " + playerUID + ". Deleting.");
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_OUTSIDE_ZONE));
            GetGame().ObjectDelete(this);
            return;
        }

        // FIX G-2: Territorio ajeno
        if (mgr.IsPositionInOtherTerritory(position, groupID))
        {
            LFPG_Log.Error("ItemBase build in other territory by " + playerUID + ". Deleting.");
            mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_OTHER_TERRITORY));
            GetGame().ObjectDelete(this);
            return;
        }

        // Greenhouse routing: si el item esta en la whitelist, contar como garden/plot
        bool isGreenhouse = LFPG_IsGreenhousePlot();

        if (isGreenhouse)
        {
            if (!mgr.CanPlaceGarden(groupID))
            {
                LFPG_Log.Error("Garden/greenhouse limit reached for group " + groupID);
                mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_GARDEN_LIMIT));
                GetGame().ObjectDelete(this);
                return;
            }

            // FIX C-3: Idempotencia
            if (LFPG_DeployTracker.IsGardenTracked(this))
                return;

            string ghMsg = "[SimpleGroup] Greenhouse counted as plot: ";
            ghMsg = ghMsg + GetType();
            ghMsg = ghMsg + " for group ";
            ghMsg = ghMsg + groupID;
            PrintToRPT(ghMsg);
            mgr.IncrementGardenCount(groupID);
            LFPG_DeployTracker.TrackGarden(this, groupID);
        }
        else
        {
            if (!mgr.CanDeploy(groupID))
            {
                LFPG_Log.Error("Deploy limit reached for group " + groupID);
                mgr.SendErrorToPlayer(identity, pb, LFPG_GetBlockReasonMsg(LFPG_BLOCK_DEPLOY_LIMIT));
                GetGame().ObjectDelete(this);
                return;
            }

            // FIX C-3: Idempotencia
            if (LFPG_DeployTracker.IsTracked(this))
                return;

            string deployMsg = "[SimpleGroup] Deploy counted: ";
            deployMsg = deployMsg + GetType();
            deployMsg = deployMsg + " for group ";
            deployMsg = deployMsg + groupID;
            PrintToRPT(deployMsg);
            mgr.IncrementDeployCount(groupID);
            LFPG_DeployTracker.Track(this, groupID);
        }
        #endif
    }

    // Decrementar via tracker map.
    // Si el item fue recogido, EEItemLocationChanged ya hizo Untrack.
    // Aqui Untrack retorna "" -> no double-decrement. Sin allocaciones.
    override void EEDelete(EntityAI parent)
    {
        #ifdef SERVER
        // BBB/Garden tienen sus propios EEDelete handlers
        if (!IsInherited(BaseBuildingBase) && !IsInherited(GardenPlot))
        {
            // Intentar garden tracker primero (greenhouses), luego deploy tracker
            string gardenGroup = LFPG_DeployTracker.UntrackGarden(this);
            if (gardenGroup != "")
            {
                LFPG_GroupManager mgrG = LFPG_GroupManager.Get();
                if (mgrG)
                {
                    mgrG.DecrementGardenCount(gardenGroup);
                }
            }
            else
            {
                string trackedGroup = LFPG_DeployTracker.Untrack(this);
                if (trackedGroup != "")
                {
                    LFPG_GroupManager mgr = LFPG_GroupManager.Get();
                    if (mgr)
                    {
                        mgr.DecrementDeployCount(trackedGroup);
                    }
                }
            }
        }
        #endif

        super.EEDelete(parent);
    }

    // FIX E: Conteo y restriccion de drops de muebles deployables
    // Separa CONTEO (siempre activo) de RESTRICCION (configurable)
    // Dispara con G key, drag-to-vicinity, y cualquier movimiento a suelo
    override void EEItemLocationChanged(notnull InventoryLocation oldLoc, notnull InventoryLocation newLoc)
    {
        super.EEItemLocationChanged(oldLoc, newLoc);

        #ifdef SERVER
        int oldLocType = oldLoc.GetType();
        int newLocType = newLoc.GetType();

        // FIX I-4: Spawn inicial (oldType=UNKNOWN) no es ni pickup ni drop
        if (oldLocType == InventoryLocationType.UNKNOWN)
            return;

        // === PICKUP FROM GROUND: decrementar counter ===
        // Cuando un mueble es recogido del suelo (F, proximidad, drag),
        // oldLoc es GROUND y newLoc es CARGO/HANDS/ATTACHMENT.
        if (oldLocType == InventoryLocationType.GROUND && newLocType != InventoryLocationType.GROUND)
        {
            // BBB/Garden tienen sus propios handlers (y no se pueden "recoger")
            if (!IsInherited(BaseBuildingBase) && !IsInherited(GardenPlot))
            {
                // Tracker es fuente de verdad. Intentar garden primero, luego deploy.
                string pickupGardenID = LFPG_DeployTracker.UntrackGarden(this);
                if (pickupGardenID != "")
                {
                    LFPG_GroupManager mgrPG = LFPG_GroupManager.Get();
                    if (mgrPG)
                    {
                        string pickupGMsg = "[SimpleGroup] Pickup garden decremented: ";
                        pickupGMsg = pickupGMsg + GetType();
                        pickupGMsg = pickupGMsg + " from group ";
                        pickupGMsg = pickupGMsg + pickupGardenID;
                        PrintToRPT(pickupGMsg);
                        mgrPG.DecrementGardenCount(pickupGardenID);
                    }
                }
                else
                {
                    string pickupGroupID = LFPG_DeployTracker.Untrack(this);
                    if (pickupGroupID != "")
                    {
                        LFPG_GroupManager mgrPickup = LFPG_GroupManager.Get();
                        if (mgrPickup)
                        {
                            string pickupMsg = "[SimpleGroup] Pickup decremented: ";
                            pickupMsg = pickupMsg + GetType();
                            pickupMsg = pickupMsg + " from group ";
                            pickupMsg = pickupMsg + pickupGroupID;
                            PrintToRPT(pickupMsg);
                            mgrPickup.DecrementDeployCount(pickupGroupID);
                        }
                    }
                }
            }
            return;
        }

        // === DROP TO GROUND: logica existente ===
        if (newLocType != InventoryLocationType.GROUND)
            return;

        // Solo desde inventario/manos del jugador
        bool fromPlayer = (oldLocType == InventoryLocationType.CARGO || oldLocType == InventoryLocationType.HANDS || oldLocType == InventoryLocationType.ATTACHMENT || oldLocType == InventoryLocationType.PROXYCARGO);
        if (!fromPlayer)
            return;

        // Si viene del hologram, OnPlacementComplete ya lo maneja
        if (IsBeingPlaced())
            return;

        // BBB tiene su propio handler
        if (IsInherited(BaseBuildingBase))
            return;

        // Flag base nunca llega aqui (IsTakeable-gated) - exclusion defensiva
        string flagBaseDrop = "LFPG_FlagBase";
        if (IsKindOf(flagBaseDrop))
            return;

        // v3+: lookup de listas (una sola consulta al config)
        LFPG_GroupManager mgrLists = LFPG_GroupManager.Get();
        bool isUnrestrictedDrop = false;
        bool isNoBaseReqDrop = false;
        if (mgrLists)
        {
            LFPG_TerritoryConfig cfgLists = mgrLists.GetConfig();
            if (cfgLists)
            {
                isUnrestrictedDrop = cfgLists.IsUnrestricted(this);
                isNoBaseReqDrop = cfgLists.IsNoBaseRequired(this);
            }
        }

        // Lista B: sin restriccion alguna — no contar, no bloquear, exit
        if (isUnrestrictedDrop)
            return;

        // Solo muebles deployables o items en lista A (no-base-required)
        if (!LFPG_IsFurniture() && !isNoBaseReqDrop)
            return;

        // --- A partir de aqui: es un mueble soltado al suelo por un jugador (o aparente) ---

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return;

        // Resolver el jugador que solto el item
        EntityAI oldParent = oldLoc.GetParent();
        PlayerBase pb = null;
        if (oldParent)
        {
            pb = PlayerBase.Cast(oldParent);
            if (!pb)
            {
                EntityAI hierRoot = oldParent.GetHierarchyRoot();
                if (hierRoot)
                {
                    pb = PlayerBase.Cast(hierRoot);
                }

                // FIX D-13: Item caido de vehiculo (coche destruido con cargo).
                // El hierRoot es Transport/CarScript, no un jugador. Skip.
                if (!pb && hierRoot && (hierRoot.IsInherited(Transport) || hierRoot.IsInherited(CarScript)))
                {
                    return;
                }
            }
        }

        if (!pb)
            return;

        PlayerIdentity identity = pb.GetIdentity();
        if (!identity)
            return;

        string playerUID = identity.GetPlainId();
        string groupID = mgr.GetPlayerGroupID(playerUID);
        vector dropPos = newLoc.GetPos();

        // Determinar estado del drop respecto a territorios
        bool inOwnTerritory = false;
        bool inOtherTerritory = false;
        if (groupID != "")
        {
            inOwnTerritory = mgr.IsInBuildZone(playerUID, dropPos);
            // FIX PLACEMENT: tambien bloquear si cae en territorio ajeno, incluso si
            // se solapa con el propio (consistencia con OnPlacementComplete).
            inOtherTerritory = mgr.IsPositionInOtherTerritory(dropPos, groupID);
        }

        bool dropIsGreenhouse = LFPG_IsGreenhousePlot();

        // CONTEO: solo si esta en territorio propio, NO cae en ajeno, y hay espacio.
        // Items en lista A no cuentan (no son muebles persistentes en su flujo).
        bool canCount = inOwnTerritory && !inOtherTerritory && !isNoBaseReqDrop;

        if (canCount)
        {
            if (dropIsGreenhouse && mgr.CanPlaceGarden(groupID))
            {
                // FIX C-3: Idempotencia
                if (!LFPG_DeployTracker.IsGardenTracked(this))
                {
                    string dropGHMsg = "[SimpleGroup] Drop greenhouse counted as plot: ";
                    dropGHMsg = dropGHMsg + GetType();
                    dropGHMsg = dropGHMsg + " for group ";
                    dropGHMsg = dropGHMsg + groupID;
                    PrintToRPT(dropGHMsg);
                    mgr.IncrementGardenCount(groupID);
                    LFPG_DeployTracker.TrackGarden(this, groupID);
                }
            }
            else if (!dropIsGreenhouse && mgr.CanDeploy(groupID))
            {
                // FIX C-3: Idempotencia
                if (!LFPG_DeployTracker.IsTracked(this))
                {
                    string dropCountMsg = "[SimpleGroup] Drop counted: ";
                    dropCountMsg = dropCountMsg + GetType();
                    dropCountMsg = dropCountMsg + " for group ";
                    dropCountMsg = dropCountMsg + groupID;
                    PrintToRPT(dropCountMsg);
                    mgr.IncrementDeployCount(groupID);
                    LFPG_DeployTracker.Track(this, groupID);
                }
            }
        }

        // RESTRICCION: solo bloquear drops en territorio ajeno (anti-grief) o por
        // encima del limite en territorio propio. Drops en wilderness SIEMPRE
        // permitidos para no romper swaps (pickup mientras tienes algo en manos
        // hace auto-drop interno que no debe bloquearse nunca).
        LFPG_TerritoryConfig config = mgr.GetConfig();
        bool enforceRestrictions = false;
        if (config)
        {
            enforceRestrictions = config.m_EnforceContainerDropRestrictions;
        }

        if (enforceRestrictions)
        {
            int blockReason = LFPG_BLOCK_NONE;

            if (isNoBaseReqDrop)
            {
                // Lista A: SOLO bloquear en territorio ajeno. Wilderness/sin grupo OK.
                bool nbrInOther = false;
                if (groupID != "")
                    nbrInOther = inOtherTerritory;
                else
                    nbrInOther = mgr.IsPositionInTerritory(dropPos);

                if (nbrInOther)
                    blockReason = LFPG_BLOCK_OTHER_TERRITORY;
            }
            else if (inOtherTerritory)
            {
                // Anti-grief: no dejar muebles en el territorio de otro grupo.
                blockReason = LFPG_BLOCK_OTHER_TERRITORY;
            }
            else if (inOwnTerritory)
            {
                // En territorio propio: respetar los limites de conteo.
                if (dropIsGreenhouse && !mgr.CanPlaceGarden(groupID))
                    blockReason = LFPG_BLOCK_GARDEN_LIMIT;
                else if (!dropIsGreenhouse && !mgr.CanDeploy(groupID))
                    blockReason = LFPG_BLOCK_DEPLOY_LIMIT;
            }
            // else: wilderness drop, SIEMPRE permitido (necesario para swaps en pickup)

            if (blockReason != LFPG_BLOCK_NONE)
            {
                // Devolver item al inventario del jugador
                GameInventory inv = pb.GetInventory();
                if (inv)
                {
                    bool returned = inv.TakeEntityToInventory(InventoryMode.PREDICTIVE, FindInventoryLocationType.CARGO, this);
                    if (!returned)
                    {
                        int anySlot = FindInventoryLocationType.ANY;
                        inv.TakeEntityToInventory(InventoryMode.PREDICTIVE, anySlot, this);
                    }
                }

                // Error especifico segun motivo (en lugar del generico STR_LFPG_ERR_DROP_RESTRICTED)
                string errKey = LFPG_GetBlockReasonMsg(blockReason);
                if (errKey == "")
                    errKey = "#STR_LFPG_ERR_DROP_RESTRICTED";
                mgr.SendErrorToPlayer(identity, pb, errKey);

                string dropLog = "[SimpleGroup] Drop blocked for ";
                dropLog = dropLog + playerUID;
                dropLog = dropLog + " (";
                dropLog = dropLog + GetType();
                dropLog = dropLog + ") reason=";
                dropLog = dropLog + blockReason.ToString();
                PrintToRPT(dropLog);
            }
        }
        #endif
    }
};
