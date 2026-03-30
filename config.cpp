// ============================================================================
// LFPG_Territory — config.cpp
// Sistema de Grupos / Territorio / Restricción de Construcción
// Herencia: ItemBase (NO Flag_Base) — decisión TechRef FINAL v2 §0.1
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
// CfgVehicles — Entidades del mod
// ============================================================================

class CfgVehicles
{
    // Forward declares
    class Inventory_Base;

    // ========================================================================
    // LFPG_FlagKit_T1 — Kit crafteable y deployable (produce LFPG_Flag_T1)
    // ========================================================================
    class LFPG_FlagKit_T1: Inventory_Base
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAGKIT_T1";
        descriptionShort = "$STR_LFPG_FLAGKIT_T1_DESC";
        // Placeholder: modelo vanilla TerritoryFlagKit
        model = "\dz\gear\camping\territory_flag_kit.p3d";
        rotationFlags = 16;
        weight = 800;
        itemSize[] = { 2, 5 };
        canBeSplit = 0;
        varQuantityInit = 0;
        varQuantityMin = 0;
        varQuantityMax = 0;
    };

    // ========================================================================
    // LFPG_FlagBase — Clase base abstracta de banderas de territorio
    // Hereda Inventory_Base (script: ItemBase). NO hereda Flag_Base.
    // Flag_Base añade lógica de folded/unfolded al adjuntarse a poste
    // que no necesitamos — nuestras banderas son standalone.
    // ========================================================================
    class LFPG_FlagBase: Inventory_Base
    {
        scope = 0;
        displayName = "LFPG Flag Base";
        descriptionShort = "Base class — not spawnable";
        // storageCategory = 1 activa persistencia de la entidad
        storageCategory = 1;
        // Lifetime alto: la bandera persiste indefinidamente (controlado por nuestro sistema)
        lifetime = 3888000;
        // No se puede tomar del suelo una vez desplegada
        isMeleeWeapon = 0;
        weight = 5000;
        itemSize[] = { 10, 10 };

        // AnimationSources para raise/lower
        // Convención NUESTRA: phase 0.0 = ARRIBA, 1.0 = ABAJO (igual que vanilla flag_mast)
        // En script usamos raiseProgress (0.0=bajada, 1.0=subida) y convertimos:
        //   vanillaPhase = 1.0 - raiseProgress
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
    // LFPG_Flag_T1 — Tier 1: Palo + trapo, estructura básica
    // Attachment slots para upgrade a T2: WoodenLog + Rope
    // ========================================================================
    class LFPG_Flag_T1: LFPG_FlagBase
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAG_T1";
        descriptionShort = "$STR_LFPG_FLAG_T1_DESC";
        // Placeholder: modelo vanilla TerritoryFlag construido
        model = "\dz\structures\military\furniture\flag_pole\flag_pole.p3d";
        weight = 3000;
        itemSize[] = { 10, 10 };

        // Slots para materiales de upgrade T1 → T2
        attachments[] =
        {
            "Material_FPole_WoodenLog",
            "Material_FPole_Rope"
        };
    };

    // ========================================================================
    // LFPG_Flag_T2 — Tier 2: Tronco + bandera, estructura media
    // Attachment slots para upgrade a T3: Firewood + Nails + Stones
    // ========================================================================
    class LFPG_Flag_T2: LFPG_FlagBase
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAG_T2";
        descriptionShort = "$STR_LFPG_FLAG_T2_DESC";
        model = "\dz\structures\military\furniture\flag_pole\flag_pole.p3d";
        weight = 8000;
        itemSize[] = { 10, 10 };

        // Slots para materiales de upgrade T2 → T3
        // stackMax exactos de vanilla: Firewood=6, Nails=60, Stones=16
        attachments[] =
        {
            "Firewood",
            "Material_FPole_Nails",
            "Stones"
        };
    };

    // ========================================================================
    // LFPG_Flag_T3 — Tier 3: Fortaleza piedra + bandera, estructura avanzada
    // Sin slots de upgrade (tier máximo)
    // ========================================================================
    class LFPG_Flag_T3: LFPG_FlagBase
    {
        scope = 2;
        displayName = "$STR_LFPG_FLAG_T3";
        descriptionShort = "$STR_LFPG_FLAG_T3_DESC";
        model = "\dz\structures\military\furniture\flag_pole\flag_pole.p3d";
        weight = 15000;
        itemSize[] = { 10, 10 };
    };
};

// ============================================================================
// CfgMods — Registro de módulos de script
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
