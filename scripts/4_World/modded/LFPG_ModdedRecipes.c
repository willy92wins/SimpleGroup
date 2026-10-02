// ============================================================================
// LFPG_ModdedRecipes.c - 4_World/modded
// PluginRecipesManagerBase registers every recipe by hand. RecipeBase is not
// auto-discovered. LFPG_CraftFlagKitT1 is registered here after the vanilla list.
// ============================================================================

modded class PluginRecipesManagerBase
{
    override void RegisterRecipies()
    {
        super.RegisterRecipies();
        RegisterRecipe(new LFPG_CraftFlagKitT1);
    }
};
