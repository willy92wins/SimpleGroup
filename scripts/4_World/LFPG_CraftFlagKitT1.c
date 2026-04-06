// ============================================================================
// LFPG_CraftFlagKitT1.c - 4_World (RecipeBase auto-discovered by engine)
// Crafteo: LongWoodenStick + Rag -> LFPG_FlagKit_T1
// Patron exacto de CraftTerritoryFlagKit vanilla
// ============================================================================

class LFPG_CraftFlagKitT1 extends RecipeBase
{
    override void Init()
    {
        m_Name = "#STR_LFPG_CRAFT_FLAGKIT_T1";
        m_IsInstaRecipe = false;
        m_AnimationLength = 0.5;
        m_Specialty = -0.02;

        // Condiciones de ingredientes
        m_MinDamageIngredient[0] = -1;
        m_MaxDamageIngredient[0] = 3;
        m_MinQuantityIngredient[0] = -1;
        m_MaxQuantityIngredient[0] = -1;

        m_MinDamageIngredient[1] = -1;
        m_MaxDamageIngredient[1] = 3;
        m_MinQuantityIngredient[1] = 1;
        m_MaxQuantityIngredient[1] = -1;

        // Ingrediente 0: LongWoodenStick (se destruye)
        InsertIngredient(0, "LongWoodenStick");

        m_IngredientAddHealth[0] = 0;
        m_IngredientSetHealth[0] = -1;
        m_IngredientAddQuantity[0] = 0;
        m_IngredientDestroy[0] = true;
        m_IngredientUseSoftSkills[0] = false;

        // Ingrediente 1: Rag (consume 1)
        InsertIngredient(1, "Rag");

        m_IngredientAddHealth[1] = 0;
        m_IngredientSetHealth[1] = -1;
        m_IngredientAddQuantity[1] = -1;
        m_IngredientDestroy[1] = false;
        m_IngredientUseSoftSkills[1] = false;

        // Resultado: LFPG_FlagKit_T1
        AddResult("LFPG_FlagKit_T1");

        m_ResultSetFullQuantity[0] = false;
        m_ResultSetQuantity[0] = -1;
        m_ResultSetHealth[0] = -1;
        m_ResultInheritsHealth[0] = -2;
        m_ResultInheritsColor[0] = -1;
        m_ResultToInventory[0] = -2;
        m_ResultUseSoftSkills[0] = false;
        m_ResultReplacesIngredient[0] = -1;
    }

    override bool CanDo(ItemBase ingredients[], PlayerBase player)
    {
        // No permitir si estan attached a otro objeto
        if (ingredients[0].GetInventory().IsAttachment())
            return false;
        if (ingredients[1].GetInventory().IsAttachment())
            return false;
        return true;
    }
};
