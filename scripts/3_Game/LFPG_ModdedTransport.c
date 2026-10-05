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
};
