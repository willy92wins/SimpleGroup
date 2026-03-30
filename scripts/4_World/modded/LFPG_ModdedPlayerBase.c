// ============================================================================
// LFPG_ModdedPlayerBase.c — 4_World/modded
// Sync de datos de grupo al reconectar — usa EEInit() de la entidad jugador
//
// OPTIMIZACIÓN vs MissionGameplay.OnInit():
//  En OnInit() el PlayerBase puede no existir aún en el server.
//  EEInit() se dispara cuando la entidad del jugador está lista.
//  Garantiza que GetIdentity() funciona y podemos enviar RPCs.
// ============================================================================

modded class PlayerBase
{
    override void EEInit()
    {
        super.EEInit();

        #ifdef SERVER
        // Solo en servidor: enviar sync si el jugador tiene grupo
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(
            LFPG_DelayedPlayerSync, 2000, false, this
        );
        #endif
    }
};

// Función global para el CallLater (no se puede pasar método de modded class)
// El delay de 2s asegura que la identidad esté disponible
void LFPG_DelayedPlayerSync(PlayerBase player)
{
    if (!player)
        return;

    PlayerIdentity identity = player.GetIdentity();
    if (!identity)
        return;

    string playerUID = identity.GetPlainId();

    LFPG_GroupManager mgr = LFPG_GroupManager.Get();
    if (!mgr)
        return;

    if (!mgr.HasGroup(playerUID))
        return;

    mgr.OnPlayerJoined(playerUID, player);
}
