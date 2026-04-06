// ============================================================================
// LFPG_ModdedPlayerBase.c - 4_World/modded
// Sync de datos de grupo al reconectar - usa EEInit() de la entidad jugador
//
// OPTIMIZACION vs MissionGameplay.OnInit():
//  En OnInit() el PlayerBase puede no existir aun en el server.
//  EEInit() se dispara cuando la entidad del jugador esta lista.
//  Garantiza que GetIdentity() funciona y podemos enviar RPCs.
// ============================================================================

modded class PlayerBase
{
    override void EEInit()
    {
        super.EEInit();

        #ifdef SERVER
        // Solo en servidor: enviar sync si el jugador tiene grupo
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(LFPG_DelayedPlayerSync, 2000, false, this);
        #endif
    }
};

// Funcion global para el CallLater (no se puede pasar metodo de modded class)
// El delay de 2s asegura que la identidad este disponible
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
