// ============================================================================
// LFPG_FlagKit_T1.c — 4_World/entities
// Kit crafteable que se despliega como LFPG_Flag_T1
//
// Crafteo: LongWoodenStick + Rag → LFPG_FlagKit_T1 (via RecipeBase)
// Deploy: Hologram → coloca LFPG_Flag_T1 en el mundo
//
// EXCEPCIÓN: Este item está EXENTO de la restricción de construcción
// (el jugador no tiene grupo aún cuando despliega su primera bandera)
// ============================================================================

class LFPG_FlagKit_T1 extends ItemBase
{
    // ========================================================================
    // DEPLOY CONFIG
    // ========================================================================
    override bool IsDeployable()
    {
        return true;
    }

    // Classname de la entidad que se spawna al deployar
    string GetDeployedEntityName()
    {
        return "LFPG_Flag_T1";
    }

    // ========================================================================
    // PLACEMENT — Override vanilla CanBePlaced para añadir check de territorio
    // ========================================================================
    override bool CanBePlaced(Man player, vector position)
    {
        // Primero: checks vanilla (superficie, colisión, etc.)
        if (!super.CanBePlaced(player, position))
            return false;

        // Check territorio: no puede haber otra bandera LFPG a <500m
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            if (mgr.IsPositionInTerritory(position))
                return false;
        }

        return true;
    }

    // ========================================================================
    // ACTIONS
    // ========================================================================
    override void SetActions()
    {
        super.SetActions();
        AddAction(ActionTogglePlaceObject);
        AddAction(ActionDeployObject);
    }
};
