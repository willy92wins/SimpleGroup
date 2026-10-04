// ============================================================================
// SimpleGroup — config.cpp
// Sistema de Grupos / Territorio / Restriccion de Construccion
// Herencia: ItemBase (NO Flag_Base) — TechRef FINAL v2
//
// MODELOS:
//   T1 → SimpleGroup\data\T1\T1_Flagpole.p3d
//   T2 → SimpleGroup\data\T2\T2_Flagpole.p3d
//   T3 → SimpleGroup\data\T3\T3_Flagpole.p3d (con slot bateria)
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
        ghostIcon = "woodenlog";
        stackMax = 1;
    };
    class Slot_LFPG_FlagRope
    {
        name = "LFPG_FlagRope";
        displayName = "Rope";
        ghostIcon = "rope";
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
        stackMax = 10;
    };
    // T3 power slot (drawn by ProxyLFPG_FlagBattery, CfgNonAIVehicles below)
    class Slot_LFPG_FlagBattery
    {
        name = "LFPG_FlagBattery";
        displayName = "Car Battery";
        ghostIcon = "carbattery";
        stackMax = 1;
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
            "DZ_Vehicles_Parts",
            "DF_Scripts"
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
        model = "\SimpleGroup\data\T1\T1_FlagKit.p3d";
        projectionTypename = "LFPG_Flag_T1_Placing";
        rotationFlags = 17;
        weight = 800;
        itemSize[] = { 1, 5 };
        itemBehaviour = 1;
        canBeSplit = 0;
        varQuantityInit = 0;
        varQuantityMin = 0;
        varQuantityMax = 0;
        hiddenSelections[] = {"T1_Rag"};
        hiddenSelectionsTextures[] = {"DZ\gear\consumables\data\rag_co.paa"};
        hiddenSelectionsMaterials[] = {"DZ\gear\consumables\data\rags_bandages.rvmat"};
        hologramMaterial = "hologram";
        hologramMaterialPath = "dz\data";

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
    // LFPG_Flag_T1_Placing — Entidad SOLO para hologram preview
    // ========================================================================
    class LFPG_Flag_T1_Placing: Inventory_Base
    {
        scope = 1;
        autocenter = 0;
        // Own copy of the T1 model: a .p3d shared with LFPG_Flag_T1 (physLayer item_large) gets two physics layers.
        model = "\SimpleGroup\data\T1\T1_Flagpole_Placing.p3d";
        storageCategory = 10;
        alignHologramToTerain = 0;
        hiddenSelections[] = {"T1_Rag"};
        hiddenSelectionsTextures[] = {"DZ\gear\consumables\data\rag_co.paa"};
        hiddenSelectionsMaterials[] = {"DZ\gear\consumables\data\rags_bandages.rvmat"};

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
    // LFPG_FlagBase — Base abstracta de banderas
    // ========================================================================
    class LFPG_FlagBase: Inventory_Base
    {
        scope = 0;
        autocenter = 0;
        displayName = "LFPG Flag Base";
        descriptionShort = "Base class - not spawnable";
        storageCategory = 1;
        lifetime = 3888000;
        isMeleeWeapon = 0;
        weight = 5000;
        itemSize[] = { 10, 10 };
        physLayer = "item_large";
        carveNavmesh = 1;
        isDeployable = 0;

        class AnimationSources
        {
            class flag_mast
            {
                source = "user";
                animPeriod = 0.5;
                initPhase = 1;
            };
        };

        class DamageSystem
        {
            class GlobalHealth
            {
                class Health
                {
                    hitpoints = 1000;
                };
            };
            class DamageZones
            {
                class Body
                {
                    class Health
                    {
                        hitpoints = 1000;
                        transferToGlobalCoef = 1.0;
                    };
                    componentNames[] = {"Component01"};
                    fatalInjuryCoef = -1;
                };
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
        model = "\SimpleGroup\data\T1\T1_Flagpole.p3d";
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
    // Material_FPole_Flag: vanilla flag (any Flag_Base), required by the upgrade
    // to T3 and moved to the T3 by it; not drawn on the T2 model.
    // ========================================================================
    class LFPG_Flag_T2: LFPG_FlagBase
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAG_T2";
        descriptionShort = "$STR_LFPG_FLAG_T2_DESC";
        model = "\SimpleGroup\data\T2\T2_Flagpole.p3d";
        weight = 8000;
        itemSize[] = { 10, 10 };

        attachments[] =
        {
            "LFPG_FlagFirewood",
            "LFPG_FlagNails",
            "LFPG_FlagStones",
            "Material_FPole_Flag"
        };
        class GUIInventoryAttachmentsProps
        {
            class UpgradeMaterials
            {
                name = "Upgrade to T3";
                description = "";
                attachmentSlots[] = {"LFPG_FlagFirewood", "LFPG_FlagNails", "LFPG_FlagStones", "Material_FPole_Flag"};
                icon = "set:dayz_inventory image:cat_common_cargo";
            };
        };
    };

    // ========================================================================
    // LFPG_Flag_T3 — Tier 3 (max, sin slots de upgrade)
    // Slot de bateria para sistema de energia
    // Material_FPole_Flag: vanilla flag drawn by the vanilla DZ_Flag proxy on
    // flag_mast; locked while the flag is not fully lowered (LFPG_Flag_T3.c)
    // ========================================================================
    class LFPG_Flag_T3: LFPG_FlagBase
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAG_T3";
        descriptionShort = "$STR_LFPG_FLAG_T3_DESC";
        model = "\SimpleGroup\data\T3\T3_Flagpole.p3d";
        weight = 15000;
        itemSize[] = { 10, 10 };
        // Index 0: the battery cables, shown only while a battery is attached (LFPG_Flag_T3.c)
        simpleHiddenSelections[] = { "battery_cables" };

        attachments[] = { "LFPG_FlagBattery", "Material_FPole_Flag" };
        class GUIInventoryAttachmentsProps
        {
            class PowerSupply
            {
                name = "Power Supply";
                description = "";
                attachmentSlots[] = {"LFPG_FlagBattery"};
                icon = "set:dayz_inventory image:cat_common_cargo";
            };
            // Same category as the vanilla TerritoryFlag (DZ\gear\camping\config.cpp)
            class Flag
            {
                name = "$STR_CfgVehicles_TerritoryFlag_Att_Category_Flag";
                description = "";
                attachmentSlots[] = {"Material_FPole_Flag"};
                icon = "set:dayz_inventory image:tf_flag";
            };
        };
    };

    // ========================================================================
    // Vanilla item overrides — anadir inventorySlot custom
    // Desde DayZ 1.07+ todos los items usan inventorySlot[] (array)
    // += funciona correctamente en todos los casos
    // ========================================================================
    class WoodenLog: Inventory_Base
    {
        inventorySlot[] += {"LFPG_FlagLog"};
    };
    class Rope: Inventory_Base
    {
        inventorySlot[] += {"LFPG_FlagRope"};
    };
    class Firewood: Inventory_Base
    {
        inventorySlot[] += {"LFPG_FlagFirewood"};
    };
    class Nail: Inventory_Base
    {
        inventorySlot[] += {"LFPG_FlagNails"};
    };
    class Stone: Inventory_Base
    {
        inventorySlot[] += {"LFPG_FlagStones"};
    };
    class CarBattery: Inventory_Base
    {
        inventorySlot[] += {"LFPG_FlagBattery"};
    };
};

// ============================================================================
// CfgNonAIVehicles — draw an attached car battery at the T3 battery proxy
// Own single-slot attachment proxy, as the vanilla flag (ProxyDZ_Flag): the
// vehicle part proxy ProxyBattery_Car draws nothing on an ItemBase. The class
// name is "Proxy" + the model basename of proxy:\SimpleGroup\data\T3\LFPG_FlagBattery.001
// ============================================================================

class CfgNonAIVehicles
{
    class ProxyAttachment;
    class ProxyLFPG_FlagBattery: ProxyAttachment
    {
        scope = 2;
        inventorySlot[] = {"LFPG_FlagBattery"};
        model = "\SimpleGroup\data\T3\LFPG_FlagBattery.p3d";
    };
};

// ============================================================================
// CfgMods
// ============================================================================

class CfgMods
{
    class SimpleGroup
    {
        dir = "SimpleGroup";
        name = "SimpleGroup";
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
