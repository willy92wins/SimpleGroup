// ============================================================================
// LFPG_ModdedVehicles.c - 4_World/modded
// Vehicle protection state, synchronised to clients for the cursor icon.
//
// Transport is an engine class and cannot be modded, so the state lives on the
// two script vehicle bases, CarScript and BoatScript, which every vanilla
// vehicle derives from. LFPG_VehicleState reaches it from a Transport.
// The server sets it while the vehicle holds a protected slot of a raised
// group flag (LFPG_VehicleProtection). Clients only read it.
// ============================================================================

class LFPG_VehicleState
{
    // Vehicles that can hold a protected slot.
    static bool IsProtectable(Transport vehicle)
    {
        if (CarScript.Cast(vehicle))
            return true;
        if (BoatScript.Cast(vehicle))
            return true;
        return false;
    }

    static bool IsProtected(Transport vehicle)
    {
        CarScript car = CarScript.Cast(vehicle);
        if (car)
            return car.LFPG_IsProtected();
        BoatScript boat = BoatScript.Cast(vehicle);
        if (boat)
            return boat.LFPG_IsProtected();
        return false;
    }

    // Server only.
    static void SetProtected(Transport vehicle, bool state)
    {
        CarScript car = CarScript.Cast(vehicle);
        if (car)
        {
            car.LFPG_SetProtected(state);
            return;
        }
        BoatScript boat = BoatScript.Cast(vehicle);
        if (boat)
            boat.LFPG_SetProtected(state);
    }

    // Vanilla resets the economy lifetime of the root entity to its default when
    // items inside it are combined (ItemBase.OnCombine -> IncreaseLifetimeUp). A
    // protected vehicle keeps what it had if the reset lowered it; the validation
    // tick tops it up again. Returns true when this call wrote the log line.
    static bool KeepLifetime(EntityAI vehicle, float lifeBefore, bool alreadyLogged)
    {
        float lifeReset = vehicle.GetLifetime();
        if (lifeReset >= lifeBefore)
            return false;
        vehicle.SetLifetime(lifeBefore);
        if (alreadyLogged)
            return false;
        string resetMsg = "Vanilla lifetime reset undone on a protected vehicle: ";
        resetMsg = resetMsg + vehicle.GetType();
        resetMsg = resetMsg + " before=" + lifeBefore.ToString();
        resetMsg = resetMsg + " reset=" + lifeReset.ToString();
        resetMsg = resetMsg + " now=" + vehicle.GetLifetime().ToString();
        LFPG_Log.Info(resetMsg);
        return true;
    }
};

modded class CarScript
{
    // True while the vehicle holds a protected slot of a raised group flag.
    protected bool m_LFPG_ProtectedNet;
    // A restored vanilla lifetime reset was logged for this vehicle. Server only.
    protected bool m_LFPG_ResetLogged;

    void CarScript()
    {
        m_LFPG_ProtectedNet = false;
        m_LFPG_ResetLogged = false;

        string varProtected = "m_LFPG_ProtectedNet";
        RegisterNetSyncVariableBool(varProtected);
    }

    bool LFPG_IsProtected()
    {
        return m_LFPG_ProtectedNet;
    }

    // Server only. The vehicle is marked dirty only when the state changes.
    void LFPG_SetProtected(bool state)
    {
        #ifdef SERVER
        if (m_LFPG_ProtectedNet == state)
            return;
        m_LFPG_ProtectedNet = state;
        SetSynchDirty();
        #endif
    }

    override void IncreaseLifetimeUp()
    {
        #ifdef SERVER
        float lfpgLifeBefore = GetLifetime();
        #endif

        super.IncreaseLifetimeUp();

        #ifdef SERVER
        if (m_LFPG_ProtectedNet && LFPG_VehicleState.KeepLifetime(this, lfpgLifeBefore, m_LFPG_ResetLogged))
            m_LFPG_ResetLogged = true;
        #endif
    }
};

modded class BoatScript
{
    // True while the vehicle holds a protected slot of a raised group flag.
    protected bool m_LFPG_ProtectedNet;
    // A restored vanilla lifetime reset was logged for this vehicle. Server only.
    protected bool m_LFPG_ResetLogged;

    void BoatScript()
    {
        m_LFPG_ProtectedNet = false;
        m_LFPG_ResetLogged = false;

        string varProtected = "m_LFPG_ProtectedNet";
        RegisterNetSyncVariableBool(varProtected);
    }

    bool LFPG_IsProtected()
    {
        return m_LFPG_ProtectedNet;
    }

    // Server only. The vehicle is marked dirty only when the state changes.
    void LFPG_SetProtected(bool state)
    {
        #ifdef SERVER
        if (m_LFPG_ProtectedNet == state)
            return;
        m_LFPG_ProtectedNet = state;
        SetSynchDirty();
        #endif
    }

    override void IncreaseLifetimeUp()
    {
        #ifdef SERVER
        float lfpgLifeBefore = GetLifetime();
        #endif

        super.IncreaseLifetimeUp();

        #ifdef SERVER
        if (m_LFPG_ProtectedNet && LFPG_VehicleState.KeepLifetime(this, lfpgLifeBefore, m_LFPG_ResetLogged))
            m_LFPG_ResetLogged = true;
        #endif
    }
};
