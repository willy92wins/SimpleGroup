// ============================================================================
// LFPG_ModdedHologram.c — 4_World/modded
// Restricción TOTAL de construcción via modded Hologram
//
// OPTIMIZACIÓN:
//  - FlagKit exemption via GetType() string compare — O(1), no IsKindOf
//    (modded class NO puede añadir member variables → no hay cache bool)
//  - Client: usa LFPG_ClientGroupCache para check O(1) sin RPC
//  - Server: usa LFPG_GroupManager.IsInBuildZone() O(1)
//  - Sin sqrt: DistanceSq en el cache del cliente
//
// Hook: EvaluateCollision() — se ejecuta cada frame con hologram activo
// ============================================================================

modded class Hologram
{
    override void EvaluateCollision(ItemBase action_item = null)
    {
        // Ejecutar checks vanilla primero
        super.EvaluateCollision(action_item);

        // Si vanilla ya lo bloqueó, no hacer nada más
        // m_IsColliding es accesible desde modded class (protected)
        if (m_IsColliding)
            return;

        // m_Parent es el item en manos del jugador (el kit, no la proyección)
        if (!m_Parent)
            return;

        // EXCEPCIÓN: FlagKit_T1 está exento de restricción de territorio
        // String compare con GetType() es O(1) — no usa IsKindOf
        string parentType = m_Parent.GetType();
        string kitType = "LFPG_FlagKit_T1";
        if (parentType == kitType)
            return;

        // CHECK DE BUILD ZONE
        if (!IsDedicatedServer())
        {
            // Client-side: check rápido via cache — O(1)
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
        }
    }
};
