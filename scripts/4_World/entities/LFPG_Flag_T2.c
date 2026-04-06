// ============================================================================
// LFPG_Flag_T2.c - 4_World/entities
// Tier 2: Tronco + bandera, estructura media
// Attachment slots: Firewood + Nails + Stones (para upgrade a T3)
// ============================================================================

class LFPG_Flag_T2 extends LFPG_FlagBase
{
    override int GetTier()
    {
        return 2;
    }

    override void SetActions()
    {
        super.SetActions();
        AddAction(LFPG_ActionUpgradeT3);
    }
};
