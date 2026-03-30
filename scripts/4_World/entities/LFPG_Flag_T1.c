// ============================================================================
// LFPG_Flag_T1.c — 4_World/entities
// Tier 1: Palo + trapo, estructura básica
// Attachment slots: WoodenLog + Rope (para upgrade a T2)
// Todo lo demás se hereda de LFPG_FlagBase
// ============================================================================

class LFPG_Flag_T1 extends LFPG_FlagBase
{
    override int GetTier()
    {
        return 1;
    }

    override void SetActions()
    {
        super.SetActions();
        AddAction(LFPG_ActionUpgradeT2);
    }
};
