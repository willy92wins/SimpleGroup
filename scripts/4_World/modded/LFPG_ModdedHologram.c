// ============================================================================
// LFPG_ModdedHologram.c — 4_World/modded
// Restricción TOTAL de construcción via modded Hologram
//
// OPTIMIZACIÓN:
//  - FlagKit exemption via GetType() string compare — O(1), no IsKindOf
//    (modded class NO puede añadir member variables -> no hay cache bool)
//  - Client: usa LFPG_ClientGroupCache para check O(1) sin RPC
//  - Server: usa LFPG_GroupManager.IsInBuildZone() O(1)
//  - Sin sqrt: DistanceSq en el cache del cliente
//  - Deploy limit check client-side (hologram rojo si max alcanzado)
//  - GardenPlot excluido del deploy limit (tiene su propio counter)
//
// Hook: EvaluateCollision() — se ejecuta cada frame con hologram activo
// ============================================================================

modded class Hologram
{
    override void EvaluateCollision(ItemBase action_item = null)
    {
        // Ejecutar checks vanilla primero
        super.EvaluateCollision(action_item);

        // Si vanilla ya lo bloqueo, no hacer nada mas
        // m_IsColliding es accesible desde modded class (protected)
        if (m_IsColliding)
            return;

        // m_Parent es el item en manos del jugador (el kit, no la proyeccion)
        if (!m_Parent)
            return;

        // EXCEPCION: FlagKit_T1 esta exento de restriccion de territorio
        // String compare con GetType() es O(1) — no usa IsKindOf
        string parentType = m_Parent.GetType();
        string kitType = "LFPG_FlagKit_T1";
        if (parentType == kitType)
            return;

        // CHECK DE BUILD ZONE + DEPLOY LIMIT (client-side)
        if (!IsDedicatedServer())
        {
            // Sin grupo = no construyes
            if (!LFPG_ClientGroupCache.HasGroup())
            {
                SetIsColliding(true);
                return;
            }

            // Fuera del radio de 30m o bandera bajada = no construyes
            vector projPos = GetProjectionPosition();
            if (!LFPG_ClientGroupCache.IsInBuildZone(projPos))
            {
                SetIsColliding(true);
                return;
            }

            // Deploy limit — hologram rojo si alcanzaste el maximo
            // GardenPlot tiene su propio counter, excluirlo del deploy check
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
        }
    }
};
