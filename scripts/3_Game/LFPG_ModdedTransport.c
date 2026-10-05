// ============================================================================
// LFPG_ModdedTransport.c - 3_Game
// Vehicle protection state, synchronised to clients for the cursor icon.
//
// The server sets it while the vehicle holds a protected slot of a raised
// group flag (LFPG_VehicleProtection). Clients only read it.
// ============================================================================

modded class Transport
{
    // True while the vehicle holds a protected slot of a raised group flag.
    protected bool m_LFPG_ProtectedNet;

    void Transport()
    {
        m_LFPG_ProtectedNet = false;

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

    // Vanilla resets the economy lifetime of the root entity to its default when
    // items inside it are combined (ItemBase.OnCombine -> IncreaseLifetimeUp). A
    // protected vehicle keeps what it had if that was more; the validation tick
    // tops it up again.
    override void IncreaseLifetimeUp()
    {
        #ifdef SERVER
        float lfpgLifeBefore = GetLifetime();
        #endif

        super.IncreaseLifetimeUp();

        #ifdef SERVER
        if (m_LFPG_ProtectedNet)
        {
            float lfpgLifeReset = GetLifetime();
            if (lfpgLifeReset < lfpgLifeBefore)
                SetLifetime(lfpgLifeBefore);
            string resetMsg = "Vanilla lifetime reset on a protected vehicle: ";
            resetMsg = resetMsg + GetType();
            resetMsg = resetMsg + " before=" + lfpgLifeBefore.ToString();
            resetMsg = resetMsg + " reset=" + lfpgLifeReset.ToString();
            resetMsg = resetMsg + " now=" + GetLifetime().ToString();
            LFPG_Log.Info(resetMsg);
        }
        #endif
    }
};
