// ============================================================================
// LFPG_Flag_T3.c - 4_World/entities
// Tier 3: Fortaleza — tier maximo con sistema de energia
//
// ENERGIA:
//   Bateria de coche (slot LFPG_FlagBattery): drena a m_BatteryDrainPerSecond.
//   LFPowerGrid consumer (10 u/s): via duck-typing #ifdef LFPowerGrid.
//   Grid tiene prioridad: si hay grid, la bateria NO drena.
//   Con energia la bandera se auto-sube al 100% y no baja.
//
// DUCK-TYPING:
//   No hereda LFPG_DeviceBase (herencia simple → LFPG_FlagBase).
//   Declara metodos LFPG_* que el DeviceAPI detecta via reflexion.
// ============================================================================

class LFPG_Flag_T3 extends LFPG_FlagBase
{
    // ========================================================================
    // BATTERY FIELDS (siempre presentes, sin #ifdef)
    // ========================================================================
    protected bool m_HasBatteryPower;
    protected ref Timer m_BatteryDrainTimer;

    // ========================================================================
    // POWERGRID FIELDS (declarados siempre para persistencia estable)
    // ========================================================================
    protected bool m_GridPowered;
    // FIX M-3: protected (antes eran publicos)
    protected int m_DeviceIdLow;
    protected int m_DeviceIdHigh;
    protected string m_DeviceId;

    #ifdef LFPowerGrid
    protected bool m_LFPG_Deleting;
    #endif

    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================
    void LFPG_Flag_T3()
    {
        m_HasBatteryPower = false;
        m_GridPowered = false;
        m_DeviceIdLow = 0;
        m_DeviceIdHigh = 0;
        m_DeviceId = "";

        #ifdef LFPowerGrid
        m_LFPG_Deleting = false;

        string varIdLow = "m_DeviceIdLow";
        RegisterNetSyncVariableInt(varIdLow);

        string varIdHigh = "m_DeviceIdHigh";
        RegisterNetSyncVariableInt(varIdHigh);
        #endif
    }

    void ~LFPG_Flag_T3()
    {
        if (m_BatteryDrainTimer)
        {
            m_BatteryDrainTimer.Stop();
            m_BatteryDrainTimer = null;
        }
    }

    // ========================================================================
    // TIER
    // ========================================================================
    override int GetTier()
    {
        return 3;
    }

    // ========================================================================
    // POWER — Estado combinado bateria + grid
    // ========================================================================
    override bool IsPowered()
    {
        return (m_HasBatteryPower || m_GridPowered);
    }

    // Evalua estado combinado y dispara transiciones
    // FIX G-12: Solo auto-raise en la transicion OFF->ON, no en cada tick
    // donde el estado sigue siendo ON (intermitencias de bateria no resetean)
    protected void UpdatePowerState()
    {
        #ifdef SERVER
        bool nowPowered = IsPowered();
        bool wasNetPowered = m_IsPoweredNet;

        if (nowPowered != wasNetPowered)
        {
            OnPowerStateChanged(nowPowered);
            m_IsPoweredNet = nowPowered;

            // Auto-raise: solo en la transicion OFF -> ON (latch)
            // Evita resets continuos si la bateria parpadea
            if (nowPowered && !wasNetPowered)
            {
                SetFullyRaised();
            }

            SetSynchDirty();
        }
        #endif
    }

    // ========================================================================
    // BATTERY — Attachment / detachment / drain
    // ========================================================================
    override void EEItemAttached(EntityAI item, string slot_name)
    {
        super.EEItemAttached(item, slot_name);

        if (slot_name == "LFPG_FlagBattery")
            LFPG_SetBatteryCablesVisible(true);

        #ifdef SERVER
        if (slot_name == "LFPG_FlagBattery")
        {
            CheckBatteryPower();
            if (m_HasBatteryPower)
            {
                StartBatteryDrainTimer();
            }
            UpdatePowerState();
        }
        #endif
    }

    override void EEItemDetached(EntityAI item, string slot_name)
    {
        super.EEItemDetached(item, slot_name);

        if (slot_name == "LFPG_FlagBattery")
            LFPG_SetBatteryCablesVisible(false);

        #ifdef SERVER
        if (slot_name == "LFPG_FlagBattery")
        {
            m_HasBatteryPower = false;
            StopBatteryDrainTimer();
            UpdatePowerState();
        }
        #endif
    }

    // Battery cables: simpleHiddenSelections[0] in config.cpp, shown only while a battery is attached.
    // Called on server and client; the client renders it.
    protected void LFPG_SetBatteryCablesVisible(bool visible)
    {
        SetSimpleHiddenSelectionState(0, visible);
    }

    protected void LFPG_RefreshBatteryCables()
    {
        string slotName = "LFPG_FlagBattery";
        EntityAI batteryEnt = FindAttachmentBySlotName(slotName);
        bool hasBattery = false;
        if (batteryEnt)
            hasBattery = true;

        LFPG_SetBatteryCablesVisible(hasBattery);
    }

    // ========================================================================
    // VANILLA FLAG — slot Material_FPole_Flag, drawn by the DZ_Flag proxy on flag_mast
    // ========================================================================
    // As on the vanilla TerritoryFlag, the flag can be put on or taken off only while the flag
    // is fully lowered. UpdateAnimationPhase runs on the server after every raise-progress write
    // (storage load and upgrade swap included) and on the client from OnVariablesSynchronized,
    // so both sides lock the slot from the same progress.
    override protected void UpdateAnimationPhase(float raiseProgress)
    {
        super.UpdateAnimationPhase(raiseProgress);

        string slotName = "Material_FPole_Flag";
        int slotId = InventorySlots.GetSlotIdFromString(slotName);
        GetInventory().SetSlotLock(slotId, raiseProgress > 0.0);
    }

    protected void CheckBatteryPower()
    {
        string slotName = "LFPG_FlagBattery";
        EntityAI batteryEnt = FindAttachmentBySlotName(slotName);
        if (!batteryEnt)
        {
            m_HasBatteryPower = false;
            return;
        }

        ItemBase battery = ItemBase.Cast(batteryEnt);
        if (!battery)
        {
            m_HasBatteryPower = false;
            return;
        }

        // FIX G-11: Una bateria ruined no alimenta (consistencia con vehiculos vanilla)
        if (battery.IsRuined())
        {
            m_HasBatteryPower = false;
            return;
        }

        ComponentEnergyManager cem = battery.GetCompEM();
        if (!cem || cem.GetEnergy() <= 0.0)
        {
            m_HasBatteryPower = false;
            return;
        }

        m_HasBatteryPower = true;
    }

    protected void StartBatteryDrainTimer()
    {
        if (!m_BatteryDrainTimer)
        {
            m_BatteryDrainTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        }
        m_BatteryDrainTimer.Run(10.0, this, "OnBatteryDrainTick", null, true);
    }

    protected void StopBatteryDrainTimer()
    {
        if (m_BatteryDrainTimer)
        {
            m_BatteryDrainTimer.Stop();
        }
    }

    void OnBatteryDrainTick()
    {
        #ifdef SERVER
        // Si el grid alimenta, la bateria no drena (grid prioridad)
        if (m_GridPowered)
            return;

        string slotName = "LFPG_FlagBattery";
        EntityAI batteryEnt = FindAttachmentBySlotName(slotName);
        if (!batteryEnt)
        {
            m_HasBatteryPower = false;
            StopBatteryDrainTimer();
            UpdatePowerState();
            return;
        }

        ItemBase battery = ItemBase.Cast(batteryEnt);
        if (!battery || battery.IsRuined())
        {
            m_HasBatteryPower = false;
            StopBatteryDrainTimer();
            UpdatePowerState();
            return;
        }

        ComponentEnergyManager cem = battery.GetCompEM();
        if (!cem)
        {
            m_HasBatteryPower = false;
            StopBatteryDrainTimer();
            UpdatePowerState();
            return;
        }

        float currentEnergy = cem.GetEnergy();
        if (currentEnergy <= 0.0)
        {
            m_HasBatteryPower = false;
            StopBatteryDrainTimer();
            UpdatePowerState();
            return;
        }

        // Drenar bateria
        LFPG_TerritoryConfig config = GetTerritoryConfig();
        float drainPerSec = 0.01;
        if (config)
            drainPerSec = config.m_BatteryDrainPerSecond;

        float drainAmount = drainPerSec * 10.0;
        float newEnergy = currentEnergy - drainAmount;
        if (newEnergy < 0.0)
            newEnergy = 0.0;

        cem.SetEnergy(newEnergy);

        if (newEnergy <= 0.0)
        {
            m_HasBatteryPower = false;
            StopBatteryDrainTimer();
        }
        else
        {
            m_HasBatteryPower = true;
        }

        UpdatePowerState();
        #endif
    }

    // ========================================================================
    // PERSISTENCE
    // ========================================================================
    override void OnStoreSave(ParamsWriteContext ctx)
    {
        super.OnStoreSave(ctx);

        // v2: device identity para LFPowerGrid (siempre escrito para formato estable)
        ctx.Write(m_DeviceIdLow);
        ctx.Write(m_DeviceIdHigh);
    }

    override bool OnStoreLoad(ParamsReadContext ctx, int version)
    {
        if (!super.OnStoreLoad(ctx, version))
            return false;

        // v2+: leer device identity (SIMETRICO con OnStoreSave que siempre escribe)
        // Si storage version indica v2+, AMBOS ints deben existir o el stream esta corrupto.
        // Previamente se toleraba lectura parcial -> CORRUPCION del stream para la siguiente entity.
        if (m_LoadedStorageVersion >= 2)
        {
            int devLow = 0;
            if (!ctx.Read(devLow))
            {
                LFPG_Log.Error("T3 storage load: failed to read m_DeviceIdLow");
                return false;
            }
            int devHigh = 0;
            if (!ctx.Read(devHigh))
            {
                LFPG_Log.Error("T3 storage load: failed to read m_DeviceIdHigh");
                return false;
            }
            m_DeviceIdLow = devLow;
            m_DeviceIdHigh = devHigh;
        }

        return true;
    }

    override void AfterStoreLoad()
    {
        super.AfterStoreLoad();

        LFPG_RefreshBatteryCables();

        #ifdef SERVER
        CheckBatteryPower();
        if (m_HasBatteryPower)
        {
            StartBatteryDrainTimer();
        }
        UpdatePowerState();
        #endif
    }

    // ========================================================================
    // LIFECYCLE
    // ========================================================================
    override void EEInit()
    {
        super.EEInit();

        LFPG_RefreshBatteryCables();

        #ifdef LFPowerGrid
        #ifdef SERVER
        if (m_DeviceIdLow == 0 && m_DeviceIdHigh == 0)
        {
            int genLow = 0;
            int genHigh = 0;
            LFPG_Util.GenerateDeviceId(genLow, genHigh);
            m_DeviceIdLow = genLow;
            m_DeviceIdHigh = genHigh;
            SetSynchDirty();
        }
        #endif

        LFPG_TryRegister();
        #endif
    }

    // ========================================================================
    // LFPOWERGRID — Device interface via duck-typing
    // Todo el bloque se compila SOLO si el mod LFPowerGrid esta cargado.
    // ========================================================================
    #ifdef LFPowerGrid

    // --- Lifecycle ---
    override void EEDelete(EntityAI parent)
    {
        m_LFPG_Deleting = true;
        LFPG_DeviceLifecycle.OnDeviceDeleted(this, m_DeviceId);
        super.EEDelete(parent);
    }

    override void EEItemLocationChanged(notnull InventoryLocation oldLoc, notnull InventoryLocation newLoc)
    {
        super.EEItemLocationChanged(oldLoc, newLoc);

        #ifdef SERVER
        bool wiresCut = LFPG_DeviceLifecycle.OnDeviceMoved(this, m_DeviceId, oldLoc, newLoc);
        if (wiresCut)
        {
            m_GridPowered = false;
            UpdatePowerState();
        }
        #endif
    }

    override void OnVariablesSynchronized()
    {
        super.OnVariablesSynchronized();
        LFPG_TryRegister();
    }

    // --- Registration ---
    protected void LFPG_UpdateDeviceIdString()
    {
        m_DeviceId = LFPG_Util.MakeDeviceKey(m_DeviceIdLow, m_DeviceIdHigh);
    }

    protected void LFPG_TryRegister()
    {
        if (m_LFPG_Deleting)
            return;

        string oldId = m_DeviceId;
        LFPG_UpdateDeviceIdString();

        if (oldId != "" && oldId != m_DeviceId)
        {
            LFPG_DeviceRegistry.Get().Unregister(oldId, this);
        }

        if (m_DeviceId != "")
        {
            LFPG_DeviceRegistry.Get().Register(this, m_DeviceId);
        }
    }

    // --- IDevice: Identity ---
    string LFPG_GetDeviceId()
    {
        return m_DeviceId;
    }

    int LFPG_GetDeviceType()
    {
        return LFPG_DeviceType.CONSUMER;
    }

    bool LFPG_IsSource()
    {
        return false;
    }

    bool LFPG_HasWireStore()
    {
        return false;
    }

    // --- IDevice: Ports ---
    int LFPG_GetPortCount()
    {
        return 1;
    }

    string LFPG_GetPortName(int idx)
    {
        if (idx == 0)
            return "input_power";
        return "";
    }

    int LFPG_GetPortDir(int idx)
    {
        if (idx == 0)
            return LFPG_PortDir.IN;
        return -1;
    }

    string LFPG_GetPortLabel(int idx)
    {
        if (idx == 0)
            return "Power Input";
        return "";
    }

    bool LFPG_HasPort(string portName, int dir)
    {
        if (dir != LFPG_PortDir.IN)
            return false;
        if (portName == "input_power")
            return true;
        return false;
    }

    vector LFPG_GetPortWorldPos(string portName)
    {
        vector basePos = GetPosition();
        if (portName == "input_power")
        {
            // FIX D-16: 2.5m arriba para estar por encima del tronco real de T3 (~4m alto)
            basePos[1] = basePos[1] + 2.5;
        }
        return basePos;
    }

    // --- IDevice: Energy ---
    float LFPG_GetConsumption()
    {
        LFPG_TerritoryConfig config = GetTerritoryConfig();
        if (config)
            return config.m_PowerGridConsumption;
        return 10.0;
    }

    // --- IDevice: Power state ---
    void LFPG_SetPowered(bool powered)
    {
        #ifdef SERVER
        if (m_GridPowered != powered)
        {
            m_GridPowered = powered;
            UpdatePowerState();
        }
        #endif
    }

    bool LFPG_IsPowered()
    {
        return IsPowered();
    }

    bool LFPG_GetOverloaded()
    {
        return false;
    }

    void LFPG_SetOverloaded(bool val)
    {
        // Consumer sin logica de overload
    }

    #endif
    // ========================================================================
    // FIN #ifdef LFPowerGrid
    // ========================================================================
};
