// ============================================================================
// LFPG_ModdedItemRegisterCallbacks.c - 4_World/modded
// In-hands profile of the T1 flag kit: the same one-handed animation set and IK
// as the vanilla LongWoodenStick (see DayZPlayerTypeRegisterItems in
// dayzplayercfgbase.c, which passes the tools one-handed behaviour here).
// ============================================================================

modded class ModItemRegisterCallbacks
{
    override void RegisterOneHanded(DayZPlayerType pType, DayzPlayerItemBehaviorCfg pBehavior)
    {
        super.RegisterOneHanded(pType, pBehavior);

        pType.AddItemInHandsProfileIK("LFPG_FlagKit_T1", "dz/anims/workspaces/player/player_main/weapons/player_main_1h_pipe.asi", pBehavior, "dz/anims/anm/player/ik/gear/LongWoodenStick.anm");
    }
};
