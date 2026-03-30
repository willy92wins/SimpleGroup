// ============================================================================
// LFPG_FlagBase.c — 4_World/entities
// Entidad base de banderas de territorio LFPG
//
// Hereda ItemBase (NO Flag_Base) — TechRef FINAL v2 §0.1
//
// OPTIMIZACIONES:
//  - raiseProgress calculado lazy (on-demand desde timestamp, sin timer global)
//  - SyncVars mínimos: float + bool + int
//  - GroupName via RPC (RegisterNetSyncVariableString no existe)
//  - Rate limiting de RPCs integrado
//  - Fase conversion: raiseProgress(0=down,1=up) ↔ vanillaPhase(0=up,1=down)
//
// PERSISTENCIA:
//  - m_GroupID (string) — enlace al grupo en JSON
//  - m_RemainingSeconds (float) — tiempo restante de raise
//    Se persiste el remaining, NO epoch. Así no dependemos de reloj real.
//    La bandera solo baja durante uptime del servidor (igual que vanilla).
// ============================================================================

class LFPG_FlagBase extends ItemBase
{
    // ========================================================================
    // PERSISTED FIELDS — guardados en OnStoreSave, restaurados en OnStoreLoad
    // ========================================================================
    protected string m_GroupID;
    protected float m_RemainingSeconds;

    // ========================================================================
    // SYNCVARS — server → clients (automático vía engine)
    // Registrados en constructor. Escritura SOLO en #ifdef SERVER.
    // ========================================================================
    float m_RaiseProgressNet;
    bool m_InviteModeNet;
    int m_MemberCountNet;

    // ========================================================================
    // RUNTIME FIELDS — NO persistidos, NO sincronizados
    // ========================================================================
    protected int m_RaisedAtTime;
    protected float m_RemainingAtRaise;
    protected bool m_IsRegisteredWithManager;

    // ========================================================================
    // CONSTRUCTOR — Registro de SyncVars (DEBE ser aquí, NO en EEInit)
    // ========================================================================
    void LFPG_FlagBase()
    {
        m_GroupID = "";
        m_RemainingSeconds = 0.0;
        m_RaiseProgressNet = 0.0;
        m_InviteModeNet = false;
        m_MemberCountNet = 0;
        m_RaisedAtTime = 0;
        m_RemainingAtRaise = 0.0;
        m_IsRegisteredWithManager = false;

        // SyncVar registration — string var names assigned to locals first
        string varProgress = "m_RaiseProgressNet";
        RegisterNetSyncVariableFloat(varProgress, 0.0, 1.0, 5);

        string varInvite = "m_InviteModeNet";
        RegisterNetSyncVariableBool(varInvite);

        string varMembers = "m_MemberCountNet";
        RegisterNetSyncVariableInt(varMembers, 0, 20);
    }

    void ~LFPG_FlagBase()
    {
        // Cancelar CallLater pendiente de DeactivateInviteMode
        // Si no se cancela y la entidad se destruye con invite activo → segfault
        if (GetGame())
        {
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(DeactivateInviteMode);
        }
    }

    // ========================================================================
    // TIER — Override en subclases
    // ========================================================================
    int GetTier()
    {
        return 1;
    }

    // ========================================================================
    // GROUP ID — Acceso desde GroupManager
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
    // RAISE PROGRESS — Sistema lazy (sin timer global)
    //
    // Convención LFPG: 0.0 = bajada, 1.0 = subida
    // Convención vanilla AnimationPhase: 0.0 = arriba, 1.0 = abajo
    //
    // Conversión:  vanillaPhase = 1.0 - raiseProgress
    // ========================================================================

    // Calcula el raise progress ACTUAL basado en tiempo transcurrido
    // Llamar on-demand: en checks de build zone, al sincronizar, etc.
    float ComputeCurrentRaiseProgress()
    {
        // Si nunca fue subida o no tiene remaining, está bajada
        if (m_RemainingAtRaise <= 0.0)
            return 0.0;

        int now = GetGame().GetTime();
        float elapsedMs = now - m_RaisedAtTime;
        float elapsedS = elapsedMs * 0.001;
        float remaining = m_RemainingAtRaise - elapsedS;

        if (remaining <= 0.0)
            return 0.0;

        // Obtener duración total del tier desde config
        LFPG_TerritoryConfig config = GetTerritoryConfig();
        float tierDuration = 172800.0;
        if (config)
        {
            tierDuration = config.GetTierDuration(GetTier());
        }

        if (tierDuration <= 0.0)
            tierDuration = 172800.0;

        float progress = remaining / tierDuration;
        progress = Math.Clamp(progress, 0.0, 1.0);
        return progress;
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

        m_RaisedAtTime = GetGame().GetTime();
        m_RemainingAtRaise = tierDuration;
        m_RemainingSeconds = tierDuration;

        m_RaiseProgressNet = 1.0;
        SetSynchDirty();

        UpdateAnimationPhase(1.0);
        #endif
    }

    // Incrementa el progress (durante acción de subir bandera)
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

    // Decrementa el progress (durante acción de bajar bandera)
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

    // ¿Está la bandera completamente subida? (para checks de restricciones)
    bool IsFullyRaised()
    {
        float progress = ComputeCurrentRaiseProgress();
        return (progress >= 1.0);
    }

    // ¿Está la bandera completamente bajada? (restricciones OFF)
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
    // INVITE MODE
    // ========================================================================
    void ActivateInviteMode(int durationMs)
    {
        #ifdef SERVER
        m_InviteModeNet = true;
        SetSynchDirty();

        // One-shot CallLater es seguro (no afectado por bug 4.5h)
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(
            DeactivateInviteMode, durationMs, false
        );
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
    // MEMBER COUNT (SyncVar para UI rápida)
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
    // CONFIG — Acceso a la config del servidor via GroupManager
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

    // ========================================================================
    // PERSISTENCE — OnStoreSave / OnStoreLoad
    //
    // Versión de storage para forward-compatibility.
    // Al añadir campos nuevos, incrementar LFPG_STORAGE_VERSION y
    // manejar migración en OnStoreLoad.
    // ========================================================================
    override void OnStoreSave(ParamsWriteContext ctx)
    {
        super.OnStoreSave(ctx);

        // Version
        ctx.Write(LFPG_STORAGE_VERSION);

        // v1 fields
        ctx.Write(m_GroupID);

        // Guardar remaining actualizado al momento del save
        float currentRemaining = 0.0;
        if (m_RemainingAtRaise > 0.0)
        {
            int now = GetGame().GetTime();
            float elapsedMs = now - m_RaisedAtTime;
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

        // v1 fields
        string groupID = "";
        if (!ctx.Read(groupID))
            return false;
        m_GroupID = groupID;

        float remaining = 0.0;
        if (!ctx.Read(remaining))
            return false;
        m_RemainingSeconds = remaining;

        return true;
    }

    override void AfterStoreLoad()
    {
        super.AfterStoreLoad();

        // Restaurar estado de raise desde datos persistidos
        m_RaisedAtTime = GetGame().GetTime();
        m_RemainingAtRaise = m_RemainingSeconds;

        // Recalcular y sincronizar progress
        float progress = ComputeCurrentRaiseProgress();
        m_RaiseProgressNet = progress;
        UpdateAnimationPhase(progress);

        // Registrar con el GroupManager si tiene grupo
        if (HasGroup())
        {
            RegisterWithManager();
        }

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
            mgr.RegisterFlag(this, m_GroupID);
            m_IsRegisteredWithManager = true;
        }
    }

    // ========================================================================
    // SYNCVAR CALLBACK (client-side) — Reaccionar a cambios
    // ========================================================================
    override void OnVariablesSynchronized()
    {
        super.OnVariablesSynchronized();

        // Actualizar visual de la bandera según raise progress recibido
        UpdateAnimationPhase(m_RaiseProgressNet);
    }

    // ========================================================================
    // RPC DISPATCHER
    // Se ruteará al GroupManager para la lógica de negocio.
    // La bandera es solo el TARGET del RPC, no contiene lógica de grupo.
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

        // Client procesa S2C RPCs (via cache estático, no GroupManager)
        if (!IsDedicatedServer())
        {
            LFPG_ClientGroupCache.HandleClientRPC(rpc_type, ctx, this);
        }
    }

    // ========================================================================
    // ACTIONS — Se configuran en subclases y aquí
    // ========================================================================
    override void SetActions()
    {
        super.SetActions();

        AddAction(LFPG_ActionRaiseFlag);
        AddAction(LFPG_ActionLowerFlag);
        AddAction(LFPG_ActionInvite);
        AddAction(LFPG_ActionJoinGroup);
        AddAction(LFPG_ActionDestroyFlag);
    }

    // ========================================================================
    // UTILITY
    // ========================================================================

    // Para checks rápidos en hologram: ¿este jugador puede construir aquí?
    bool IsPlayerInBuildZone(string playerUID, vector buildPos)
    {
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (!mgr)
            return false;
        return mgr.IsInBuildZone(playerUID, buildPos);
    }

    // Datos de upgrade transferidos a la nueva bandera durante el swap
    void TransferDataFrom(LFPG_FlagBase oldFlag)
    {
        if (!oldFlag)
            return;

        m_GroupID = oldFlag.GetGroupID();
        m_RemainingSeconds = 0.0;
        m_RemainingAtRaise = 0.0;
        m_RaisedAtTime = GetGame().GetTime();

        // La nueva bandera empieza completamente bajada tras upgrade
        // El jugador debe subirla de nuevo manualmente
        m_RaiseProgressNet = 0.0;
        m_InviteModeNet = false;
        m_MemberCountNet = oldFlag.GetMemberCount();
    }

    // Debug: imprimir estado actual
    void DebugPrintState()
    {
        string msg = "[LFPG_Flag] GroupID=";
        msg = msg + m_GroupID;
        msg = msg + " Tier=";
        msg = msg + GetTier().ToString();
        msg = msg + " Progress=";
        msg = msg + ComputeCurrentRaiseProgress().ToString();
        msg = msg + " Members=";
        msg = msg + m_MemberCountNet.ToString();
        Print(msg);
    }
};
