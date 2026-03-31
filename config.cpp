// ============================================================================
// LFPG_Territory — config.cpp
// Sistema de Grupos / Territorio / Restriccion de Construccion
// Herencia: ItemBase (NO Flag_Base) — TechRef FINAL v2
//
// PLACEHOLDER MODELS:
//   Kit T1  → LongWoodenStick (palo largo)
//   Banderas → WoodenCrate (cajon de madera)
//   VERIFICAR RUTAS .p3d antes de empaquetar!
// ============================================================================

class CfgPatches
{
    class LFPG_Territory
    {
        units[] =
        {
            "LFPG_FlagKit_T1",
            "LFPG_Flag_T1",
            "LFPG_Flag_T2",
            "LFPG_Flag_T3"
        };
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] =
        {
            "DZ_Data",
            "DZ_Scripts",
            "DZ_Gear_Camping",
            "JM_CF_Scripts"
        };
    };
};

// ============================================================================
// CfgVehicles
// ============================================================================

class CfgVehicles
{
    class Inventory_Base;

    // ========================================================================
    // LFPG_FlagKit_T1 — Kit crafteable y deployable
    // Placeholder: modelo LongWoodenStick
    // ========================================================================
    class LFPG_FlagKit_T1: Inventory_Base
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAGKIT_T1";
        descriptionShort = "$STR_LFPG_FLAGKIT_T1_DESC";
        // TODO: verificar ruta exacta en tu P: drive
        model = "\dz\gear\crafting\wooden_stick_blunt.p3d";
        rotationFlags = 16;
        weight = 800;
        itemSize[] = { 1, 5 };
        canBeSplit = 0;
        varQuantityInit = 0;
        varQuantityMin = 0;
        varQuantityMax = 0;
    };

    // ========================================================================
    // LFPG_FlagBase — Base abstracta de banderas
    // ========================================================================
    class LFPG_FlagBase: Inventory_Base
    {
        scope = 0;
        displayName = "LFPG Flag Base";
        descriptionShort = "Base class - not spawnable";
        storageCategory = 1;
        lifetime = 3888000;
        isMeleeWeapon = 0;
        weight = 5000;
        itemSize[] = { 10, 10 };

        class AnimationSources
        {
            class flag_mast
            {
                source = "user";
                animPeriod = 0.5;
                initPhase = 1;
            };
        };
    };

    // ========================================================================
    // LFPG_Flag_T1 — Tier 1
    // Placeholder: modelo WoodenCrate
    // Slots para upgrade a T2: WoodenLog + Rope
    // ========================================================================
    class LFPG_Flag_T1: LFPG_FlagBase
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAG_T1";
        descriptionShort = "$STR_LFPG_FLAG_T1_DESC";
        // TODO: verificar ruta exacta en tu P: drive
        model = "\dz\gear\camping\wooden_case.p3d";
        weight = 3000;
        itemSize[] = { 10, 10 };

        attachments[] =
        {
            "Material_FPole_WoodenLog",
            "Material_FPole_Rope"
        };
    };

    // ========================================================================
    // LFPG_Flag_T2 — Tier 2
    // Slots para upgrade a T3: Firewood + Nails + Stones
    // ========================================================================
    class LFPG_Flag_T2: LFPG_FlagBase
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAG_T2";
        descriptionShort = "$STR_LFPG_FLAG_T2_DESC";
        model = "\dz\gear\camping\wooden_case.p3d";
        weight = 8000;
        itemSize[] = { 10, 10 };

        attachments[] =
        {
            "Firewood",
            "Material_FPole_Nails",
            "Stones"
        };
    };

    // ========================================================================
    // LFPG_Flag_T3 — Tier 3 (max, sin slots de upgrade)
    // ========================================================================
    class LFPG_Flag_T3: LFPG_FlagBase
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAG_T3";
        descriptionShort = "$STR_LFPG_FLAG_T3_DESC";
        model = "\dz\gear\camping\wooden_case.p3d";
        weight = 15000;
        itemSize[] = { 10, 10 };
    };
};

// ============================================================================
// CfgMods
// ============================================================================

class CfgMods
{
    class LFPG_Territory
    {
        type = "mod";
        dependencies[] = { "Game", "World", "Mission" };

        class defs
        {
            class gameScriptModule
            {
                value = "";
                files[] = { "LFPG_Territory/scripts/3_Game" };
            };
            class worldScriptModule
            {
                value = "";
                files[] = { "LFPG_Territory/scripts/4_World" };
            };
            class missionScriptModule
            {
                value = "";
                files[] = { "LFPG_Territory/scripts/5_Mission" };
            };
        };
    };
};
