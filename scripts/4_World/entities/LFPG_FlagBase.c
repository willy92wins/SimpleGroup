// ============================================================================
// LFPG_FlagBase.c - 4_World/entities
// Entidad base de banderas de territorio LFPG
//
// Hereda ItemBase (NO Flag_Base) - TechRef FINAL v2 S0.1
//
// OPTIMIZACIONES:
//  - raiseProgress calculado lazy (on-demand desde timestamp, sin timer global)
//  - SyncVars minimos: float + bool + int
//  - GroupName via RPC (RegisterNetSyncVariableString no existe)
//  - Rate limiting de RPCs integrado
//  - Fase conversion: raiseProgress(0=down,1=up) <-> vanillaPhase(0=up,1=down)
//
// PERSISTENCIA:
//  - m_GroupID (string) - enlace al grupo en JSON
//  - m_RemainingSeconds (float) - tiempo restante de raise
//    Se persiste el remaining, NO epoch. Asi no dependemos de reloj real.
//    La bandera solo baja durante uptime del servidor (igual que vanilla).
// ============================================================================

class LFPG_FlagBase extends ItemBase
{
    // Group-owned flags stay in the CE for 45 days and are topped up while registered.
    static const float LFPG_GROUP_FLAG_LIFETIME = 3888000.0;

    // ========================================================================
    // PERSISTED FIELDS - guardados en OnStoreSave, restaurados en OnStoreLoad
    // ========================================================================
    protected string m_GroupID;
    protected float m_RemainingSeconds;

    // ========================================================================
    // SYNCVARS - server -> clients (automatico via engine)
    // Registrados en constructor. Escritura SOLO en #ifdef SERVER.
    // ========================================================================
    float m_RaiseProgressNet;
    bool m_InviteModeNet;
    int m_MemberCountNet;
    bool m_IsPoweredNet;
    bool m_FlagActionsEnabledNet;

    // ========================================================================
    // RUNTIME FIELDS - NO persistidos, NO sincronizados
    // ========================================================================
    protected int m_RaisedAtTime;
    protected float m_RemainingAtRaise;
    protected bool m_IsRegisteredWithManager;
    protected int m_LoadedStorageVersion;
    // AUDIT #10 L1-F05: estado calculado sin config, pendiente de rehacer.
    protected bool m_ConfigReinitQueued;
    protected bool m_FullRaisePendingConfig;
    protected bool m_GroupLifetimeLogged;
    protected bool m_FailedLoadLifetimeLogged;
    // Filled by OnStoreLoad. Copied onto the live fields in AfterStoreLoad,
    // which runs only after the whole entity load, including LFPG_Flag_T3, succeeds.
    protected string m_PendingGroupID;
    protected float m_PendingRemainingSeconds;
    protected bool m_HasPendingStore;

    // ========================================================================
    // CONSTRUCTOR - Registro de SyncVars (DEBE ser aqui, NO en EEInit)
    // ========================================================================
    void LFPG_FlagBase()
    {
        m_GroupID = "";
        m_RemainingSeconds = 0.0;
        m_RaiseProgressNet = 0.0;
        m_InviteModeNet = false;
        m_MemberCountNet = 0;
        m_IsPoweredNet = false;
        m_FlagActionsEnabledNet = true;
        m_RaisedAtTime = 0;
        m_RemainingAtRaise = 0.0;
        m_IsRegisteredWithManager = false;
        m_LoadedStorageVersion = 0;
        m_GroupLifetimeLogged = false;
        m_FailedLoadLifetimeLogged = false;
        m_PendingGroupID = "";
        m_PendingRemainingSeconds = 0.0;
        m_HasPendingStore = false;
        m_SkipDissolveOnDelete = false;

        // SyncVar registration - string var names assigned to locals first
        string varProgress = "m_RaiseProgressNet";
        RegisterNetSyncVariableFloat(varProgress, 0.0, 1.0, 8);

        string varInvite = "m_InviteModeNet";
        RegisterNetSyncVariableBool(varInvite);

        string varMembers = "m_MemberCountNet";
        RegisterNetSyncVariableInt(varMembers, 0, 20);

        string varPowered = "m_IsPoweredNet";
        RegisterNetSyncVariableBool(varPowered);

        string varActions = "m_FlagActionsEnabledNet";
        RegisterNetSyncVariableBool(varActions);
    }

    void ~LFPG_FlagBase()
    {
        // Cancelar CallLater pendiente de DeactivateInviteMode
        // Si no se cancela y la entidad se destruye con invite activo -> segfault
        if (GetGame())
        {
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(DeactivateInviteMode);
        }
    }

    // ========================================================================
    // EEDelete - Disolver grupo INMEDIATAMENTE al destruirse la bandera
    // m_SkipDissolveOnDelete: se activa durante UpgradeFlag para evitar
    // que ObjectDelete de la bandera vieja disuelva el grupo recien upgradeado
    // ========================================================================
    protected bool m_SkipDissolveOnDelete;

    void SetSkipDissolveOnDelete()
    {
        m_SkipDissolveOnDelete = true;
    }

    override void EEDelete(EntityAI parent)
    {
        #ifdef SERVER
        LFPG_GroupManager mgrShut = LFPG_GroupManager.Get();
        if (mgrShut && mgrShut.IsShuttingDown())
        {
            // Apagado del servidor: un wipe de entidades disparia N EEDelete y
            // vaciaria groups.json. El save final ya lo hizo OnMissionFinish.
            string shutMsg = "EEDelete: skip dissolve (server shutting down) for group=";
            shutMsg = shutMsg + m_GroupID;
            LFPG_Log.Debug(shutMsg);
        }
        else if (m_SkipDissolveOnDelete)
        {
            string skipMsg = "EEDelete: skip dissolve (upgrade) for group=";
            skipMsg = skipMsg + m_GroupID;
            LFPG_Log.Debug(skipMsg);
        }
        else if (m_GroupID != "")
        {
            LFPG_GroupManager mgr = LFPG_GroupManager.Get();
            if (mgr)
            {
                // Only the flag the manager still has registered for this group
                // may dissolve it. A duplicate with the same id only drops its
                // own pending/abandoned cache row.
                LFPG_FlagBase registeredFlag = mgr.GetGroupFlag(m_GroupID);
                if (registeredFlag == this)
                {
                    string dissolveMsg = "EEDelete: dissolving group=";
                    dissolveMsg = dissolveMsg + m_GroupID;
                    LFPG_Log.Info(dissolveMsg);
                    mgr.DissolveGroup(m_GroupID);
                }
                else
                {
                    mgr.ReleaseUnregisteredFlag(this);
                    string keepMsg = "EEDelete: not the registered flag, group kept=";
                    keepMsg = keepMsg + m_GroupID;
                    LFPG_Log.Info(keepMsg);
                }
            }
            else
            {
                LFPG_Log.Error("EEDelete: GroupManager null, cannot dissolve!");
            }
        }
        else
        {
            // FIX BUG2: Bandera abandonada despawneando - limpiar cache de posicion
            LFPG_GroupManager mgr2 = LFPG_GroupManager.Get();
            if (mgr2)
            {
                mgr2.RemoveAbandonedFlagPosition(GetPosition());
                LFPG_Log.Debug("EEDelete: abandoned flag cleanup at position");
            }
            else
            {
                LFPG_Log.Debug("EEDelete: flag has no group (m_GroupID empty)");
            }
        }
        #endif

        super.EEDelete(parent);
    }

    // ========================================================================
    // TIER - Override en subclases
    // ========================================================================
    int GetTier()
    {
        return 1;
    }

    // ========================================================================
    // FIX M-13: Contract publico para que OTROS mods puedan detectar banderas LFPG
    // sin IsKindOf("LFPG_FlagBase"). Mods raid / expansion territory pueden chequear:
    //     if (obj.IsTerritoryFlag()) { ... }
    // via ScriptCallQueue / reflection / interface duck-typing.
    // ========================================================================
    bool IsTerritoryFlag()
    {
        return true;
    }

    // ========================================================================
    // INVENTORY RESTRICTIONS - Las banderas NO se pueden coger
    // El modelo placeholder (wooden_case) hereda acciones de pickup;
    // estas overrides lo impiden.
    // ========================================================================
    override bool CanPutInCargo(EntityAI parent)
    {
        return false;
    }

    override bool CanPutIntoHands(EntityAI parent)
    {
        return false;
    }

    // IsTakeable DEBE retornar true para que el ActionManagerClient
    // incluya esta entidad en el pipeline de action targeting.
    // Con IsTakeable=false, el engine filtra ItemBase antes de evaluar
    // ActionCondition -> ninguna accion aparece jamas.
    // La bandera NO se puede coger gracias a las 3 capas:
    //   1. CanPutInCargo() = false
    //   2. CanPutIntoHands() = false
    //   3. RemoveAction(ActionTakeItem/ActionTakeItemToHands) en SetActions()
    override bool IsTakeable()
    {
        return true;
    }

    // ========================================================================
    // GROUP ID - Acceso desde GroupManager
    // ========================================================================
    string GetGroupID()
    {
        return m_GroupID;
    }

    void SetGroupID(string groupID)
    {
        m_GroupID = groupID;
    }

    bool HasGroup()
    {
        if (m_GroupID == "")
            return false;
        return true;
    }

    // ========================================================================
    // POWER - Virtual, override en T3. Base siempre retorna false.
    // ========================================================================
    bool IsPowered()
    {
        return false;
    }

    // Re-ancla timestamps cuando el estado de energia cambia.
    // Al encender: congela remaining en el valor actual.
    // Al apagar: reanuda decay desde el remaining congelado.
    void OnPowerStateChanged(bool powered)
    {
        #ifdef SERVER
        if (powered)
        {
            float currentRemaining = 0.0;
            if (m_RemainingAtRaise > 0.0)
            {
                int nowP = GetGame().GetTime();
                int rawDiffP = nowP - m_RaisedAtTime;
                if (rawDiffP < 0)
                    rawDiffP = 0;
                float elapsedMsP = rawDiffP;
                float elapsedSP = elapsedMsP * 0.001;
                currentRemaining = m_RemainingAtRaise - elapsedSP;
                if (currentRemaining < 0.0)
                    currentRemaining = 0.0;
            }
            m_RemainingAtRaise = currentRemaining;
            m_RaisedAtTime = GetGame().GetTime();
            m_RemainingSeconds = m_RemainingAtRaise;
        }
        else
        {
            m_RaisedAtTime = GetGame().GetTime();
            m_RemainingSeconds = m_RemainingAtRaise;
        }
        #endif
    }

    // ========================================================================
    // RAISE PROGRESS - Sistema lazy (sin timer global)
    //
    // Convencion LFPG: 0.0 = bajada, 1.0 = subida
    // Convencion vanilla AnimationPhase: 0.0 = arriba, 1.0 = abajo
    //
    // Conversion:  vanillaPhase = 1.0 - raiseProgress
    // ========================================================================

    // Calcula el raise progress ACTUAL basado en tiempo transcurrido
    // Si IsPowered(), el tiempo no pasa (decay congelado).
    float ComputeCurrentRaiseProgress()
    {
        // Si nunca fue subida o no tiene remaining, esta bajada
        if (m_RemainingAtRaise <= 0.0)
            return 0.0;

        // Obtener duracion total del tier desde config
        LFPG_TerritoryConfig config = GetTerritoryConfig();
        float tierDuration = 172800.0;
        if (config)
        {
            tierDuration = config.GetTierDuration(GetTier());
        }

        if (tierDuration <= 0.0)
            tierDuration = 172800.0;

        // Si esta energizada, el tiempo no pasa - usar remaining congelado
        if (IsPowered())
        {
            float progress = m_RemainingAtRaise / tierDuration;
            return Math.Clamp(progress, 0.0, 1.0);
        }

        int now = GetGame().GetTime();
        int rawDiff = now - m_RaisedAtTime;

        // Proteccion contra overflow de GetTime() (~24.85 dias uptime)
        // Si rawDiff es negativo, hubo wrap-around: re-anclar timestamps
        if (rawDiff < 0)
        {
            m_RaisedAtTime = now;
            m_RemainingSeconds = m_RemainingAtRaise;
            rawDiff = 0;
        }

        float elapsedMs = rawDiff;
        float elapsedS = elapsedMs * 0.001;
        float remaining = m_RemainingAtRaise - elapsedS;

        if (remaining <= 0.0)
            return 0.0;

        float progressDecay = remaining / tierDuration;
        progressDecay = Math.Clamp(progressDecay, 0.0, 1.0);
        return progressDecay;
    }

    // Establece la bandera como completamente subida
    // Llamar cuando un jugador termina de subirla
    void SetFullyRaised()
    {
        #ifdef SERVER
        LFPG_TerritoryConfig config = GetTerritoryConfig();
        float tierDuration = 172800.0;
        if (config)
        {
            tierDuration = config.GetTierDuration(GetTier());
        }
        else
        {
            // Boot (T3 con energia en AfterStoreLoad): el default truncaria T3.
            // Se rehace con la duracion real en OnServerConfigLoaded.
            m_FullRaisePendingConfig = true;
            QueueConfigReinit();
        }

        m_RaisedAtTime = GetGame().GetTime();
        m_RemainingAtRaise = tierDuration;
        m_RemainingSeconds = tierDuration;

        m_RaiseProgressNet = 1.0;
        SetSynchDirty();

        UpdateAnimationPhase(1.0);

        // Raise action and the T3 power latch both call this when the flag goes full.
        // The manager refreshes only when this entity is the registered flag of a live group.
        LFPG_GroupManager raiseMgr = LFPG_GroupManager.Get();
        if (raiseMgr)
        {
            raiseMgr.NotifyFlagRaised(this);
        }
        #endif
    }

    // Server only. Sets the CE lifetime used while this flag is the registered flag
    // of a live group. Logs the values once per entity, at the first application.
    void ApplyGroupLifetime()
    {
        #ifdef SERVER
        LFPG_GroupManager lifeMgr = LFPG_GroupManager.Get();
        if (!lifeMgr)
            return;
        if (!lifeMgr.IsOwnedRegisteredFlag(this))
            return;

        SetLifetimeMax(LFPG_GROUP_FLAG_LIFETIME);
        SetLifetime(LFPG_GROUP_FLAG_LIFETIME);

        if (!m_GroupLifetimeLogged)
        {
            m_GroupLifetimeLogged = true;
            string lifeMsg = "Flag lifetime at registration: remaining=";
            lifeMsg = lifeMsg + GetLifetime().ToString();
            lifeMsg = lifeMsg + " max=";
            lifeMsg = lifeMsg + GetLifetimeMax().ToString();
            lifeMsg = lifeMsg + " group=";
            lifeMsg = lifeMsg + m_GroupID;
            LFPG_Log.Info(lifeMsg);
        }
        #endif
    }

    // Server only. Failed-load sessions keep a pending flag on the same CE lifetime
    // as a registered group flag. Does not refresh nearby base objects and does not
    // treat the flag as registered. ApplyGroupLifetime still owns normal sessions.
    void ApplyFailedLoadLifetime()
    {
        #ifdef SERVER
        SetLifetimeMax(LFPG_GROUP_FLAG_LIFETIME);
        SetLifetime(LFPG_GROUP_FLAG_LIFETIME);

        if (!m_FailedLoadLifetimeLogged)
        {
            m_FailedLoadLifetimeLogged = true;
            string preserveMsg = "Flag lifetime preserved, groups file unusable: remaining=";
            preserveMsg = preserveMsg + GetLifetime().ToString();
            preserveMsg = preserveMsg + " max=";
            preserveMsg = preserveMsg + GetLifetimeMax().ToString();
            preserveMsg = preserveMsg + " group=";
            preserveMsg = preserveMsg + m_GroupID;
            LFPG_Log.Info(preserveMsg);
        }
        #endif
    }

    // Incrementa el progress (durante accion de subir bandera)
    void IncrementRaiseProgress(float delta)
    {
        #ifdef SERVER
        float current = ComputeCurrentRaiseProgress();
        float newProgress = current + delta;
        newProgress = Math.Clamp(newProgress, 0.0, 1.0);

        // Recalcular remaining basado en nuevo progress
        LFPG_TerritoryConfig config = GetTerritoryConfig();
        float tierDuration = 172800.0;
        if (config)
        {
            tierDuration = config.GetTierDuration(GetTier());
        }

        m_RemainingAtRaise = newProgress * tierDuration;
        m_RaisedAtTime = GetGame().GetTime();
        m_RemainingSeconds = m_RemainingAtRaise;

        m_RaiseProgressNet = newProgress;
        SetSynchDirty();

        UpdateAnimationPhase(newProgress);
        #endif
    }

    // Decrementa el progress (durante accion de bajar bandera)
    void DecrementRaiseProgress(float delta)
    {
        #ifdef SERVER
        float current = ComputeCurrentRaiseProgress();
        float newProgress = current - delta;
        newProgress = Math.Clamp(newProgress, 0.0, 1.0);

        LFPG_TerritoryConfig config = GetTerritoryConfig();
        float tierDuration = 172800.0;
        if (config)
        {
            tierDuration = config.GetTierDuration(GetTier());
        }

        m_RemainingAtRaise = newProgress * tierDuration;
        m_RaisedAtTime = GetGame().GetTime();
        m_RemainingSeconds = m_RemainingAtRaise;

        m_RaiseProgressNet = newProgress;
        SetSynchDirty();

        UpdateAnimationPhase(newProgress);
        #endif
    }

    // FIX AUDIT: Resetear raise progress (al abandonar bandera)
    void ResetRaiseProgress()
    {
        #ifdef SERVER
        m_RemainingAtRaise = 0.0;
        m_RaisedAtTime = 0;
        m_RemainingSeconds = 0.0;

        m_RaiseProgressNet = 0.0;
        SetSynchDirty();

        UpdateAnimationPhase(0.0);
        #endif
    }

    // ?Esta la bandera completamente subida? (para checks de restricciones)
    bool IsFullyRaised()
    {
        float progress = ComputeCurrentRaiseProgress();
        return (progress >= 1.0);
    }

    // ?Esta la bandera completamente bajada? (restricciones OFF)
    bool IsFullyLowered()
    {
        float progress = ComputeCurrentRaiseProgress();
        return (progress <= 0.0);
    }

    // Actualiza la AnimationPhase del modelo
    // Convierte de raiseProgress (0=down,1=up) a vanillaPhase (0=up,1=down)
    protected void UpdateAnimationPhase(float raiseProgress)
    {
        float vanillaPhase = 1.0 - raiseProgress;
        string animName = "flag_mast";
        SetAnimationPhase(animName, vanillaPhase);
    }

    // ========================================================================
    // VISUAL SYNC - Llamado periodicamente desde GroupManager.OnValidationTick
    // Recomputa el progress actual y actualiza SyncVar + animacion si cambio.
    // Umbral 0.005 > precision SyncVar (8 bits = 1/256 ~ 0.004)
    // ========================================================================
    void RefreshVisualProgress()
    {
        #ifdef SERVER
        float current = ComputeCurrentRaiseProgress();
        float diff = current - m_RaiseProgressNet;
        if (diff < 0.0)
            diff = -diff;

        if (diff > 0.005)
        {
            m_RaiseProgressNet = current;
            SetSynchDirty();
            UpdateAnimationPhase(current);
        }
        #endif
    }

    // ========================================================================
    // INVITE MODE
    // ========================================================================
    void ActivateInviteMode(int durationMs)
    {
        #ifdef SERVER
        m_InviteModeNet = true;
        SetSynchDirty();

        // One-shot CallLater es seguro (no afectado por bug 4.5h)
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(DeactivateInviteMode, durationMs, false);
        #endif
    }

    void DeactivateInviteMode()
    {
        #ifdef SERVER
        m_InviteModeNet = false;
        SetSynchDirty();
        #endif
    }

    bool IsInviteModeActive()
    {
        return m_InviteModeNet;
    }

    // ========================================================================
    // MEMBER COUNT (SyncVar para UI rapida)
    // ========================================================================
    void SetMemberCount(int count)
    {
        #ifdef SERVER
        m_MemberCountNet = count;
        SetSynchDirty();
        #endif
    }

    int GetMemberCount()
    {
        return m_MemberCountNet;
    }

    // ========================================================================
    // CONFIG - Acceso a la config del servidor via GroupManager
    // ========================================================================
    protected LFPG_TerritoryConfig GetTerritoryConfig()
    {
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            return mgr.GetConfig();
        }
        return null;
    }

    // Establece m_FlagActionsEnabledNet segun config del tier actual.
    // Llamar en AfterStoreLoad, TransferDataFrom y tras CreateGroup.
    void InitFlagActionsEnabled()
    {
        #ifdef SERVER
        LFPG_TerritoryConfig config = GetTerritoryConfig();
        if (config)
        {
            m_FlagActionsEnabledNet = config.IsFlagActionEnabled(GetTier());
        }
        else
        {
            // Provisional: se rehace con la config del tier en
            // OnServerConfigLoaded (antes nada lo re-inicializaba).
            m_FlagActionsEnabledNet = true;
            QueueConfigReinit();
        }
        SetSynchDirty();
        #endif
    }

    protected void QueueConfigReinit()
    {
        #ifdef SERVER
        if (m_ConfigReinitQueued)
            return;
        LFPG_GroupManager mgrCfgQ = LFPG_GroupManager.Get();
        if (!mgrCfgQ)
            return;
        mgrCfgQ.QueueFlagConfigReinit(this);
        m_ConfigReinitQueued = true;
        #endif
    }

    // Llamado por LFPG_GroupManager.Init() justo despues de cargar la config.
    void OnServerConfigLoaded()
    {
        #ifdef SERVER
        m_ConfigReinitQueued = false;
        if (m_FullRaisePendingConfig)
        {
            m_FullRaisePendingConfig = false;
            SetFullyRaised();
        }
        InitFlagActionsEnabled();
        // El progress net se calculo con la duracion por defecto (172800 s).
        RefreshVisualProgress();
        #endif
    }

    // ========================================================================
    // PERSISTENCE - OnStoreSave / OnStoreLoad
    //
    // Version de storage para forward-compatibility.
    // Al anadir campos nuevos, incrementar LFPG_STORAGE_VERSION y
    // manejar migracion en OnStoreLoad.
    // ========================================================================
    override void OnStoreSave(ParamsWriteContext ctx)
    {
        super.OnStoreSave(ctx);

        // Version
        ctx.Write(LFPG_STORAGE_VERSION);

        // v1 fields
        ctx.Write(m_GroupID);

        // Guardar remaining actualizado al momento del save
        // FIX: Si esta powered, el decay esta congelado - guardar remaining sin restar elapsed.
        // Sin este fix, una T3 powered por mas tiempo que su tier duration guardaria remaining=0.
        float currentRemaining = 0.0;
        if (IsPowered())
        {
            currentRemaining = m_RemainingAtRaise;
        }
        else if (m_RemainingAtRaise > 0.0)
        {
            int now = GetGame().GetTime();
            int rawDiff = now - m_RaisedAtTime;

            // Proteccion contra overflow de GetTime() (~24.85 dias uptime)
            if (rawDiff < 0)
            {
                rawDiff = 0;
            }

            float elapsedMs = rawDiff;
            float elapsedS = elapsedMs * 0.001;
            currentRemaining = m_RemainingAtRaise - elapsedS;
            if (currentRemaining < 0.0)
            {
                currentRemaining = 0.0;
            }
        }
        ctx.Write(currentRemaining);
    }

    override bool OnStoreLoad(ParamsReadContext ctx, int version)
    {
        if (!super.OnStoreLoad(ctx, version))
            return false;

        // Read storage version
        int storageVer = 0;
        if (!ctx.Read(storageVer))
            return false;

        // Rechazar versiones desconocidas (future-proof + defensivo)
        // Con wipe OK, mejor fallar aqui que leer bytes garbage
        if (storageVer < 1 || storageVer > LFPG_STORAGE_VERSION)
        {
            string errVer = "Unknown storage version: ";
            errVer = errVer + storageVer.ToString();
            LFPG_Log.Error(errVer);
            return false;
        }

        // v1+ fields. Keep them pending until AfterStoreLoad. LFPG_Flag_T3 reads
        // m_LoadedStorageVersion before its own OnStoreLoad returns, so the version
        // itself cannot wait.
        string groupID = "";
        if (!ctx.Read(groupID))
            return false;

        float remaining = 0.0;
        if (!ctx.Read(remaining))
            return false;

        m_LoadedStorageVersion = storageVer;
        m_PendingGroupID = groupID;
        m_PendingRemainingSeconds = remaining;
        m_HasPendingStore = true;

        return true;
    }

    override void AfterStoreLoad()
    {
        super.AfterStoreLoad();

        if (m_HasPendingStore)
        {
            m_GroupID = m_PendingGroupID;
            m_RemainingSeconds = m_PendingRemainingSeconds;
            m_HasPendingStore = false;
        }

        // Restaurar estado de raise desde datos persistidos
        m_RaisedAtTime = GetGame().GetTime();
        m_RemainingAtRaise = m_RemainingSeconds;

        // Recalcular y sincronizar progress
        float progress = ComputeCurrentRaiseProgress();
        m_RaiseProgressNet = progress;
        SetSynchDirty();
        UpdateAnimationPhase(progress);

        // Registrar con el GroupManager si tiene grupo
        if (HasGroup())
        {
            // FIX I-6: Integrity check — si el groupID no existe en el manager,
            // la flag es huerfana (grupo borrado manualmente o JSON corrupto).
            // Declararla abandonada en vez de crear registro zombi.
            LFPG_GroupManager mgrChk = LFPG_GroupManager.Get();
            if (mgrChk && mgrChk.GroupExists(m_GroupID))
            {
                RegisterWithManager();
            }
            else
            {
                // El engine restaura las entidades dentro de super.OnInit(), y
                // LoadGroups() corre DESPUES: que el grupo no exista aqui no
                // significa que sea huerfana, sino que el JSON aun no se ha leido.
                // m_GroupID NO se toca: es un campo persistido y vaciarlo romperia
                // el vinculo bandera-grupo de forma irreversible en el proximo save.
                if (mgrChk)
                {
                    mgrChk.RegisterPendingFlag(this);
                    string pendingMsg = "Flag pending, group not loaded yet: ";
                    pendingMsg = pendingMsg + m_GroupID;
                    LFPG_Log.Info(pendingMsg);
                }
            }
        }
        else
        {
            // FIX BUG2: Bandera abandonada tras restart del servidor
            // Registrar posicion como ABANDONED para bloquear territory overlap
            LFPG_GroupManager mgr = LFPG_GroupManager.Get();
            if (mgr)
            {
                mgr.RegisterAbandonedFlag(this);
            }
        }

        // Establecer SyncVar de acciones habilitadas segun config del tier
        InitFlagActionsEnabled();

        SetSynchDirty();
    }

    // ========================================================================
    // MANAGER REGISTRATION
    // ========================================================================
    protected void RegisterWithManager()
    {
        if (m_IsRegisteredWithManager)
            return;

        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            bool registered = mgr.RegisterFlag(this, m_GroupID);
            if (registered)
            {
                m_IsRegisteredWithManager = true;
            }
            else
            {
                // Registro rechazado (duplicada): desarmar el dissolve de ESTA bandera
                // para que su EEDelete no disuelva un grupo vivo.
                // m_GroupID NO se toca: es un campo persistido y borrarlo romperia
                // el vinculo bandera-grupo de forma irreversible.
                SetSkipDissolveOnDelete();
                string regRejMsg = "Flag register rejected (duplicate) for group ";
                regRejMsg = regRejMsg + m_GroupID;
                LFPG_Log.Error(regRejMsg);
            }
        }
    }

    // Wrapper publico: RegisterWithManager es protected y el manager necesita
    // poder reintentar el registro desde ResolvePendingFlags().
    void RetryRegisterWithManager()
    {
        RegisterWithManager();
    }

    // ========================================================================
    // SYNCVAR CALLBACK (client-side) - Reaccionar a cambios
    // ========================================================================
    override void OnVariablesSynchronized()
    {
        super.OnVariablesSynchronized();

        // Actualizar visual de la bandera segun raise progress recibido
        UpdateAnimationPhase(m_RaiseProgressNet);

        // FIX C3: Actualizar cache del cliente si esta bandera es la de nuestro grupo
        if (!GetGame().IsDedicatedServer())
        {
            if (LFPG_ClientGroupCache.IsFlagAtPosition(GetPosition()))
            {
                LFPG_ClientGroupCache.s_FlagRaiseProgress = m_RaiseProgressNet;
            }
        }
    }

    // ========================================================================
    // RPC DISPATCHER
    // Se ruteara al GroupManager para la logica de negocio.
    // La bandera es solo el TARGET del RPC, no contiene logica de grupo.
    // ========================================================================
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        super.OnRPC(sender, rpc_type, ctx);

        // Rango de nuestros RPCs
        if (rpc_type < 74521600 || rpc_type > 74521699)
            return;

        // Server procesa C2S RPCs
        #ifdef SERVER
        if (sender)
        {
            LFPG_GroupManager mgr = LFPG_GroupManager.Get();
            if (mgr)
            {
                mgr.HandleRPC(sender, rpc_type, ctx, this);
            }
        }
        #endif

        // Client procesa S2C RPCs (via cache estatico, no GroupManager)
        if (!GetGame().IsDedicatedServer())
        {
            LFPG_ClientGroupCache.HandleClientRPC(rpc_type, ctx, this);
        }
    }

    // ========================================================================
    // ACTIONS - Se configuran en subclases y aqui
    // ========================================================================
    override void SetActions()
    {
        super.SetActions();

        // Impedir que la bandera se pueda coger (take/drag)
        RemoveAction(ActionTakeItem);
        RemoveAction(ActionTakeItemToHands);

        AddAction(LFPG_ActionRegisterTerritory);
        AddAction(LFPG_ActionRaiseFlag);
        AddAction(LFPG_ActionLowerFlag);
        AddAction(LFPG_ActionInvite);
        AddAction(LFPG_ActionJoinGroup);
        AddAction(LFPG_ActionDestroyFlag);
        AddAction(LFPG_ActionUpgradeT2);
        AddAction(LFPG_ActionUpgradeT3);
    }

    // ========================================================================
    // UTILITY
    // ========================================================================

    // Para checks rapidos en hologram: ?este jugador puede construir aqui?
    bool IsPlayerInBuildZone(string playerUID, vector buildPos)
    {
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return false;
        return mgr.IsInBuildZone(playerUID, buildPos);
    }

    // Datos de upgrade transferidos a la nueva bandera durante el swap
    // La nueva bandera empieza fully raised con la duracion de su nuevo tier
    // FIX C-8: Reset explicito de invite state (la vieja podria tenerlo activo)
    void TransferDataFrom(LFPG_FlagBase oldFlag)
    {
        if (!oldFlag)
            return;

        m_GroupID = oldFlag.GetGroupID();

        #ifdef SERVER
        // Obtener duracion del NUEVO tier para empezar fully raised
        LFPG_TerritoryConfig config = GetTerritoryConfig();
        float tierDuration = 172800.0;
        if (config)
        {
            tierDuration = config.GetTierDuration(GetTier());
        }

        m_RaisedAtTime = GetGame().GetTime();
        m_RemainingAtRaise = tierDuration;
        m_RemainingSeconds = tierDuration;

        m_RaiseProgressNet = 1.0;
        m_InviteModeNet = false; // FIX C-8: invite reset explicito en la nueva
        m_MemberCountNet = oldFlag.GetMemberCount();

        // Establecer SyncVar de acciones habilitadas segun config del nuevo tier
        InitFlagActionsEnabled();

        SetSynchDirty();
        UpdateAnimationPhase(1.0);
        #endif
    }

    // Debug: imprimir estado actual
    void DebugPrintState()
    {
        string msg = "Flag GroupID=";
        msg = msg + m_GroupID;
        msg = msg + " Tier=";
        msg = msg + GetTier().ToString();
        msg = msg + " Progress=";
        msg = msg + ComputeCurrentRaiseProgress().ToString();
        msg = msg + " Members=";
        msg = msg + m_MemberCountNet.ToString();
        LFPG_Log.Debug(msg);
    }
};
