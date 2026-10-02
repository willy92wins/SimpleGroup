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
        // FIX M-2: delay configurable
        int delayMs = 2000;
        LFPG_GroupManager mgrInit = LFPG_GroupManager.Get();
        if (mgrInit)
        {
            LFPG_TerritoryConfig cfgInit = mgrInit.GetConfig();
            if (cfgInit && cfgInit.m_PlayerSyncDelayMs > 0)
                delayMs = cfgInit.m_PlayerSyncDelayMs;
        }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(LFPG_DelayedPlayerSync, delayMs, false, this);
        #endif
    }

    // RPCs via PlayerBase: permite enviar/recibir sin depender de la flag entity
    // Server: rutea C2S RPCs (Leave, Kick, Transfer, RequestData) al GroupManager
    // Client: rutea S2C RPCs (LightweightSync, GroupDissolved) al ClientGroupCache
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        super.OnRPC(sender, rpc_type, ctx);

        // Solo RPCs de nuestro rango
        if (rpc_type < 74521600 || rpc_type > 74521699)
            return;

        // SERVER: C2S RPCs enviados via PlayerBase (UI: leave, kick, transfer, requestdata)
        #ifdef SERVER
        if (sender)
        {
            LFPG_GroupManager mgr = LFPG_GroupManager.Get();
            if (mgr)
            {
                mgr.HandleRPC(sender, rpc_type, ctx, null);
            }
        }
        #endif

        // CLIENT: S2C RPCs enviados via PlayerBase (reconnect sync, dissolve)
        if (!GetGame().IsDedicatedServer())
        {
            LFPG_ClientGroupCache.HandleClientRPC(rpc_type, ctx, null);
        }
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

    mgr.OnPlayerJoined(playerUID, player);
}
