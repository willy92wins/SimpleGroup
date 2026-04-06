// ============================================================================
// LFPG_ModdedHologram.c - 4_World/modded
// Restriccion de construccion via modded Hologram
// LFPG_FlagKit_T1 exento (jugador sin grupo puede colocar su primera bandera)
// ============================================================================

modded class Hologram
{
    override void EvaluateCollision(ItemBase action_item = null)
    {
        super.EvaluateCollision(action_item);

        if (m_IsColliding)
            return;

        if (!m_Parent)
            return;

        string parentType = m_Parent.GetType();
        string kitType = "LFPG_FlagKit_T1";
        if (parentType == kitType)
            return;

        if (!GetGame().IsDedicatedServer())
        {
            if (!LFPG_ClientGroupCache.HasGroup())
            {
                SetIsColliding(true);
                return;
            }

            vector projPos = GetProjectionPosition();
            if (!LFPG_ClientGroupCache.IsInBuildZone(projPos))
            {
                SetIsColliding(true);
                return;
            }

            string gardenClass = "GardenPlot";
            bool isGarden = m_Parent.IsKindOf(gardenClass);
            if (!isGarden)
            {
                if (!LFPG_ClientGroupCache.CanDeploy())
                {
                    SetIsColliding(true);
                    return;
                }
            }
            else
            {
                // FIX M4: Verificar limite de garden plots client-side
                if (!LFPG_ClientGroupCache.CanPlaceGarden())
                {
                    SetIsColliding(true);
                    return;
                }
            }
        }
    }
};
