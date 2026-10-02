// ============================================================================
// LFPG_TerritoryEnums.c - 3_Game
// RPC IDs, constantes compartidas entre server y client
// ============================================================================

// RPC IDs - base alta y unica para evitar colisiones con otros mods
// Rango: 74521600 - 74521699 (100 IDs reservados)

// ----- Client -> Server -----
const int LFPG_RPC_C2S_CREATE_GROUP       = 74521600;
const int LFPG_RPC_C2S_REQUEST_JOIN       = 74521601;
const int LFPG_RPC_C2S_REQUEST_LEAVE      = 74521602;
const int LFPG_RPC_C2S_REQUEST_KICK       = 74521603;
const int LFPG_RPC_C2S_REQUEST_TRANSFER   = 74521604;
const int LFPG_RPC_C2S_START_INVITE       = 74521605;
const int LFPG_RPC_C2S_DESTROY_FLAG       = 74521606;
const int LFPG_RPC_C2S_REQUEST_GROUP_DATA = 74521607;
const int LFPG_RPC_C2S_SET_GROUP_NAME     = 74521608;

// ----- Server -> Client -----
const int LFPG_RPC_S2C_GROUP_SYNC_FULL    = 74521620;
const int LFPG_RPC_S2C_GROUP_SYNC_UPDATE  = 74521621;
const int LFPG_RPC_S2C_GROUP_DISSOLVED    = 74521622;
const int LFPG_RPC_S2C_NAME_RESULT        = 74521623;
const int LFPG_RPC_S2C_OPEN_NAME_DIALOG   = 74521624;
const int LFPG_RPC_S2C_DEPLOY_DENIED      = 74521625;
const int LFPG_RPC_S2C_ERROR_MSG          = 74521626;
const int LFPG_RPC_S2C_LIGHTWEIGHT_SYNC   = 74521627;
const int LFPG_RPC_S2C_PLACEMENT_RULES    = 74521628;

// Nonzero terminator: native serializers can return true/zero past the payload.
const int LFPG_PLACEMENT_RULES_END       = 1279676487;

// Resultado de validacion de nombre
const int LFPG_NAME_OK            = 0;
const int LFPG_NAME_TOO_SHORT     = 1;
const int LFPG_NAME_TOO_LONG      = 2;
const int LFPG_NAME_TAKEN          = 3;
const int LFPG_NAME_INVALID_CHARS  = 4;

// Tipo de sync update parcial
const int LFPG_SYNC_MEMBER_ADDED   = 0;
const int LFPG_SYNC_MEMBER_REMOVED = 1;
const int LFPG_SYNC_LEADER_CHANGED = 2;
const int LFPG_SYNC_TIER_CHANGED   = 3;
const int LFPG_SYNC_COUNT_CHANGED  = 4;

// Version de almacenamiento para OnStoreSave/OnStoreLoad
// v1: m_GroupID + m_RemainingSeconds (solo FlagBase)
// v2: + m_DeviceIdLow/High (Flag_T3, LFPowerGrid integration)
// v3: formato estable - lectura SIMETRICA con escritura.
//     Con wipe OK aceptado por el usuario, migraciones parciales fallan hard.
const int LFPG_STORAGE_VERSION = 3;

// Intervalo minimo entre RPCs del mismo jugador (ms) - anti-spam
const int LFPG_RPC_THROTTLE_MS = 500;

// Caracteres permitidos en nombre de grupo (validacion server-side)
const string LFPG_NAME_ALLOWED_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_";

// Marcador especial para banderas abandonadas en el cache de posiciones
// El prefijo # garantiza que no colisiona con IDs reales (que usan grp_)
const string LFPG_ABANDONED_GROUP = "#ABANDONED#";

// ----- Placement block reasons (cliente) -----
// Usados por LFPG_ClientGroupCache.NotifyPlacementBlocked para mostrar
// la notificacion correcta al jugador cuando el Hologram bloquea placement.
const int LFPG_BLOCK_NONE             = 0;
const int LFPG_BLOCK_NO_GROUP         = 1;
const int LFPG_BLOCK_OUTSIDE_ZONE     = 2;
const int LFPG_BLOCK_OTHER_TERRITORY  = 3;
const int LFPG_BLOCK_DEPLOY_LIMIT     = 4;
const int LFPG_BLOCK_GARDEN_LIMIT     = 5;

// Throttle min entre notificaciones del mismo motivo (evita spam por frame)
// El Hologram llama NotifyPlacementBlocked por frame mientras el holograma esta activo;
// con reset-on-valid, el usuario recibe una notificacion fresca cada vez que pasa de
// valido a invalido. Este throttle es un safety net por si el estado queda sucio.
const int LFPG_PLACEMENT_NOTIFY_THROTTLE_MS = 8000;

// Helper global: mapea razon -> clave de stringtable (notificacion al jugador)
// Usado por ClientGroupCache (toasts), Hologram, OnPlacementComplete, EEItemLocationChanged
static string LFPG_GetBlockReasonMsg(int reason)
{
    if (reason == LFPG_BLOCK_NO_GROUP)
        return "#STR_LFPG_BUILD_NO_TERRITORY";
    if (reason == LFPG_BLOCK_OUTSIDE_ZONE)
        return "#STR_LFPG_BUILD_OUTSIDE_ZONE";
    if (reason == LFPG_BLOCK_OTHER_TERRITORY)
        return "#STR_LFPG_ERR_TERRITORY_BLOCKED";
    if (reason == LFPG_BLOCK_DEPLOY_LIMIT)
        return "#STR_LFPG_BUILD_DEPLOY_LIMIT";
    if (reason == LFPG_BLOCK_GARDEN_LIMIT)
        return "#STR_LFPG_BUILD_GARDEN_LIMIT";
    return "";
}
