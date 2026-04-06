// ============================================================================
// SimpleGroup — config.cpp
// Sistema de Grupos / Territorio / Restriccion de Construccion
// Herencia: ItemBase (NO Flag_Base) — TechRef FINAL v2
//
// PLACEHOLDER MODELS:
//   Kit T1  → WoodenCrate
//   Banderas → WoodenCrate
//   VERIFICAR RUTAS .p3d antes de empaquetar!
// ============================================================================

// ============================================================================
// CfgSlots — Slots custom para upgrades (patron LFPowerGrid)
// ============================================================================
class CfgSlots
{
    // T1 -> T2 upgrade slots
    class Slot_LFPG_FlagLog
    {
        name = "LFPG_FlagLog";
        displayName = "Wooden Log";
        ghostIcon = "planks";
        stackMax = 1;
    };
    class Slot_LFPG_FlagRope
    {
        name = "LFPG_FlagRope";
        displayName = "Rope";
        ghostIcon = "metalwire";
        stackMax = 1;
    };
    // T2 -> T3 upgrade slots
    class Slot_LFPG_FlagFirewood
    {
        name = "LFPG_FlagFirewood";
        displayName = "Firewood";
        ghostIcon = "firewood";
        stackMax = 6;
    };
    class Slot_LFPG_FlagNails
    {
        name = "LFPG_FlagNails";
        displayName = "Nails";
        ghostIcon = "nails";
        stackMax = 60;
    };
    class Slot_LFPG_FlagStones
    {
        name = "LFPG_FlagStones";
        displayName = "Stones";
        ghostIcon = "stones";
        stackMax = 16;
    };
};

class CfgPatches
{
    class SimpleGroup
    {
        units[] =
        {
            "LFPG_FlagKit_T1",
            "LFPG_Flag_T1_Placing",
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
            "DZ_Gear_Consumables",
            "DZ_Gear_Crafting",
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
    // ========================================================================
    class LFPG_FlagKit_T1: Inventory_Base
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAGKIT_T1";
        descriptionShort = "$STR_LFPG_FLAGKIT_T1_DESC";
        model = "\dz\gear\camping\wooden_case.p3d";
        projectionTypename = "LFPG_Flag_T1_Placing";
        rotationFlags = 16;
        weight = 800;
        itemSize[] = { 1, 5 };
        itemBehaviour = 2;
        canBeSplit = 0;
        varQuantityInit = 0;
        varQuantityMin = 0;
        varQuantityMax = 0;
    };

    // ========================================================================
    // LFPG_Flag_T1_Placing — Entidad SOLO para hologram preview
    // ========================================================================
    class LFPG_Flag_T1_Placing: Inventory_Base
    {
        scope = 1;
        model = "\dz\gear\camping\wooden_case.p3d";
        storageCategory = 1;
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
    // Slots custom para upgrade a T2: WoodenLog + Rope
    // ========================================================================
    class LFPG_Flag_T1: LFPG_FlagBase
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAG_T1";
        descriptionShort = "$STR_LFPG_FLAG_T1_DESC";
        model = "\dz\gear\camping\wooden_case.p3d";
        weight = 3000;
        itemSize[] = { 10, 10 };

        attachments[] =
        {
            "LFPG_FlagLog",
            "LFPG_FlagRope"
        };
        class GUIInventoryAttachmentsProps
        {
            class UpgradeMaterials
            {
                name = "Upgrade to T2";
                description = "";
                attachmentSlots[] = {"LFPG_FlagLog", "LFPG_FlagRope"};
                icon = "set:dayz_inventory image:cat_common_cargo";
            };
        };
    };

    // ========================================================================
    // LFPG_Flag_T2 — Tier 2
    // Slots custom para upgrade a T3: Firewood + Nails + Stones
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
            "LFPG_FlagFirewood",
            "LFPG_FlagNails",
            "LFPG_FlagStones"
        };
        class GUIInventoryAttachmentsProps
        {
            class UpgradeMaterials
            {
                name = "Upgrade to T3";
                description = "";
                attachmentSlots[] = {"LFPG_FlagFirewood", "LFPG_FlagNails", "LFPG_FlagStones"};
                icon = "set:dayz_inventory image:cat_common_cargo";
            };
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

    // ========================================================================
    // Vanilla item overrides — anadir inventorySlot custom (patron PowerGrid)
    // IMPORTANTE: parent class explicito para no crear clase nueva scope=0
    //
    // WoodenLog y Nail usan inventorySlot[] (array) en vanilla -> += funciona
    // Rope usa inventorySlot = "Material_FPole_Rope" en vanilla (string) ->
    //   redeclarar como array incluyendo el slot vanilla original
    // Stone y Firewood usan inventorySlot = "string" en vanilla ->
    //   += falla silenciosamente (Bohemia T148506), hay que redeclarar
    //   el array completo incluyendo el slot vanilla original
    // ========================================================================
    class WoodenLog: Inventory_Base
    {
        inventorySlot[] += {"LFPG_FlagLog"};
    };
    class Rope: Inventory_Base
    {
        inventorySlot[] = {"Material_FPole_Rope", "LFPG_FlagRope"};
    };
    class Firewood: Inventory_Base
    {
        inventorySlot[] = {"Firewood", "LFPG_FlagFirewood"};
    };
    class Nail: Inventory_Base
    {
        inventorySlot[] += {"LFPG_FlagNails"};
    };
    class Stone: Inventory_Base
    {
        inventorySlot[] = {"Stones", "LFPG_FlagStones"};
        varQuantityMax = 16.0;
    };
};

// ============================================================================
// CfgMods
// ============================================================================

class CfgMods
{
    class SimpleGroup
    {
        type = "mod";
        inputs = "SimpleGroup\inputs.xml";
        dependencies[] = { "Game", "World", "Mission" };

        class defs
        {
            class gameScriptModule
            {
                value = "";
                files[] = { "SimpleGroup/scripts/3_Game" };
            };
            class worldScriptModule
            {
                value = "";
                files[] = { "SimpleGroup/scripts/4_World" };
            };
            class missionScriptModule
            {
                value = "";
                files[] = { "SimpleGroup/scripts/5_Mission" };
            };
        };
    };
};
