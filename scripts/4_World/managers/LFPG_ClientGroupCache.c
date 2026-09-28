// ============================================================================
// LFPG_ClientGroupCache.c - 4_World/managers
// Cache client-side estatico para checks rapidos sin RPC
//
// Se actualiza SOLO via RPC del server:
//  - Al conectar (LIGHTWEIGHT_SYNC)
//  - Al crear/unirse a grupo (GROUP_SYNC_FULL)
//  - Al cambiar estado de bandera
//
// NO se actualiza cada frame. Todos los campos son estaticos.
// Usado por el modded Hologram para check O(1) de build zone.
//
// FIX PLACEMENT: Sistema de notificaciones throttled para feedback al jugador
// cuando Hologram bloquea placement (outside zone, other territory, limit).
// Constantes LFPG_BLOCK_* y LFPG_PLACEMENT_NOTIFY_THROTTLE_MS viven en
// 3_Game/LFPG_TerritoryEnums.c para garantizar scope global.
// ============================================================================

class LFPG_ClientGroupCache
{
    // Datos del grupo local
    static string s_GroupID;
    static string s_GroupName;
    static string s_LeaderUID;
    static int s_Tier;
    static int s_DeployedCount;
    static int s_DeployMax;
    static int s_GardenPlotCount;

    // FIX M4: Max garden plots sincronizado desde server
    static int s_GardenPlotMax;

    // Datos de la bandera local
    static vector s_FlagPosition;
    static float s_FlagRaiseProgress;

    // Lista de miembros (para UI del panel)
    static ref array<ref LFPG_MemberData> s_Members;

    // Estado
    static bool s_HasGroup;

    // Miembros: count + max sincronizados desde server
    static int s_MemberCount;
    static int s_MaxGroupSize;

    // FIX 4: Build radius sincronizado desde server
    static float s_BuildRadiusSq;

    // Config flags sincronizados
    static bool s_EnablePlots;
    static bool s_EnableGreenhouseAsPlot;
    static ref array<string> s_GreenhouseWhitelist;

    // v3+: whitelists de placement con reglas especiales (mirror de LFPG_TerritoryConfig)
    static ref array<string> s_NoBaseRequiredTypes;  // lista A: sin grupo/zona propia OK, pero bloquea en ajena
    static ref array<string> s_UnrestrictedTypes;    // lista B: sin restriccion alguna

    // Item names para tooltip UI (solo desde full sync)
    static ref array<string> s_DeployedItemNames;
    static ref array<string> s_GardenItemNames;

    // ========================================================================
    // INIT / CLEAR
    // ========================================================================
    static void Init()
    {
        Clear();
    }

    static void Clear()
    {
        s_GroupID = "";
        s_GroupName = "";
        s_LeaderUID = "";
        s_Tier = 0;
        s_DeployedCount = 0;
        s_DeployMax = 0;
        s_GardenPlotCount = 0;
        s_GardenPlotMax = 3;
        s_MemberCount = 0;
        s_MaxGroupSize = 6;
        s_FlagPosition = vector.Zero;
        s_FlagRaiseProgress = 0.0;
        s_HasGroup = false;
        s_BuildRadiusSq = 900.0;
        s_EnablePlots = true;
        s_EnableGreenhouseAsPlot = false;
        // FIX M-24: invalidar cache del handle
        s_CachedLocalFlag = null;
        // Invalidar cache de territory check
        s_LastNearOtherValid = false;
        s_HologramResyncMs = 0;
        // FIX PLACEMENT: reset notificacion throttle
        s_LastBlockReason = LFPG_BLOCK_NONE;
        s_LastBlockNotifyMs = 0;
        if (!s_Members)
        {
            s_Members = new array<ref LFPG_MemberData>;
        }
        s_Members.Clear();
        if (!s_GreenhouseWhitelist)
        {
            s_GreenhouseWhitelist = new array<string>;
        }
        s_GreenhouseWhitelist.Clear();
        if (!s_NoBaseRequiredTypes)
        {
            s_NoBaseRequiredTypes = new array<string>;
        }
        s_NoBaseRequiredTypes.Clear();
        if (!s_UnrestrictedTypes)
        {
            s_UnrestrictedTypes = new array<string>;
        }
        s_UnrestrictedTypes.Clear();
        if (!s_DeployedItemNames)
        {
            s_DeployedItemNames = new array<string>;
        }
        s_DeployedItemNames.Clear();
        if (!s_GardenItemNames)
        {
            s_GardenItemNames = new array<string>;
        }
        s_GardenItemNames.Clear();
    }

    // ========================================================================
    // QUERIES - O(1), usadas por Hologram y UI
    // ========================================================================
    static bool HasGroup()
    {
        return s_HasGroup;
    }

    // Helper centralizado para obtener el UID del jugador local
    static string GetLocalUID()
    {
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return "";
        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return "";
        return identity.GetPlainId();
    }

    static bool IsLeader()
    {
        string localUID = GetLocalUID();
        if (localUID == "")
            return false;
        return (localUID == s_LeaderUID);
    }

    // Check rapido de build zone - O(1), sin sqrt
    // FIX 4: usa s_BuildRadiusSq sincronizado en vez de hardcoded
    static bool IsInBuildZone(vector buildPos)
    {
        if (!s_HasGroup)
            return false;

        if (s_FlagRaiseProgress <= 0.0)
            return false;

        float dx = buildPos[0] - s_FlagPosition[0];
        float dz = buildPos[2] - s_FlagPosition[2];
        float distSq = (dx * dx) + (dz * dz);

        return (distSq < s_BuildRadiusSq);
    }

    // FIX 3: Verifica si una entidad bandera en el mundo es la de nuestro grupo
    // Compara posicion con la cacheada (tolerance 2m = 4.0 distSq)
    static bool IsFlagAtPosition(vector flagPos)
    {
        if (!s_HasGroup)
            return false;

        float dx = flagPos[0] - s_FlagPosition[0];
        float dz = flagPos[2] - s_FlagPosition[2];
        float distSq = (dx * dx) + (dz * dz);
        return (distSq < 4.0);
    }

    // FIX D-15 + optimization: Cache throttled de posiciones de OTROS territorios.
    // El hologram llama EvaluateCollision por frame — GetObjectsAtPosition(600m)
    // cada frame seria catastrofico. Cacheamos el resultado por buildPos + 500ms.
    // Si el player se mueve mucho entre frames, se invalida por buildPos y se
    // recalcula; si esta quieto, solo 1 scan cada 500ms.
    static vector s_LastNearOtherPos;
    static int s_LastNearOtherMs;
    static bool s_LastNearOtherResult;
    static bool s_LastNearOtherValid;

    // Reused across scans: IsNearOtherTerritory runs on the hologram path, up to twice
    // per second, and allocated two arrays every time.
    static ref array<Object> s_ScanObjects;
    static ref array<CargoBase> s_ScanCargos;

    static bool IsNearOtherTerritory(vector buildPos)
    {
        PlayerBase plr = PlayerBase.Cast(GetGame().GetPlayer());
        if (!plr)
            return false;

        int nowMs = GetGame().GetTime();

        // Cache hit si buildPos movio < 2m y menos de 500ms desde el ultimo scan
        if (s_LastNearOtherValid)
        {
            float cdx = buildPos[0] - s_LastNearOtherPos[0];
            float cdz = buildPos[2] - s_LastNearOtherPos[2];
            float cdSq = (cdx * cdx) + (cdz * cdz);
            int timeDiff = nowMs - s_LastNearOtherMs;
            if (cdSq < 4.0 && timeDiff < 500)
                return s_LastNearOtherResult;
        }

        // Scan fresco. The loop below only accepts flags inside s_BuildRadiusSq, so asking
        // for 600 m walked 400x the needed area and every streamed object with it.
        float searchR = Math.Sqrt(s_BuildRadiusSq) + 5.0;
        if (!s_ScanObjects)
            s_ScanObjects = new array<Object>;
        if (!s_ScanCargos)
            s_ScanCargos = new array<CargoBase>;
        s_ScanObjects.Clear();
        s_ScanCargos.Clear();
        array<Object> objects = s_ScanObjects;
        GetGame().GetObjectsAtPosition(buildPos, searchR, s_ScanObjects, s_ScanCargos);

        bool result = false;
        int cnt = objects.Count();
        int i;
        for (i = 0; i < cnt; i = i + 1)
        {
            LFPG_FlagBase flag = LFPG_FlagBase.Cast(objects[i]);
            if (!flag)
                continue;

            // Es nuestra propia flag
            if (IsFlagAtPosition(flag.GetPosition()))
                continue;

            // Debe estar al menos parcialmente levantada para bloquear
            if (flag.m_RaiseProgressNet <= 0.0)
                continue;

            float fdx = buildPos[0] - flag.GetPosition()[0];
            float fdz = buildPos[2] - flag.GetPosition()[2];
            float fdSq = (fdx * fdx) + (fdz * fdz);
            if (fdSq < s_BuildRadiusSq)
            {
                result = true;
                break;
            }
        }

        s_LastNearOtherPos = buildPos;
        s_LastNearOtherMs = nowMs;
        s_LastNearOtherResult = result;
        s_LastNearOtherValid = true;
        return result;
    }

    // FIX: Helper compartido por Hologram para no necesitar var en modded class
    static int s_HologramResyncMs = 0;

    // FIX PLACEMENT: Estado del ultimo bloqueo notificado (para throttle)
    static int s_LastBlockReason = 0;
    static int s_LastBlockNotifyMs = 0;

    // Notifica al jugador sobre por que no puede colocar. Throttled por motivo+tiempo.
    // Llamado desde LFPG_ModdedHologram.EvaluateCollision (por frame mientras hologram activo)
    static void NotifyPlacementBlocked(int reason)
    {
        if (reason == LFPG_BLOCK_NONE)
        {
            // Placement OK: limpiar estado para que el siguiente bloqueo notifique fresco
            s_LastBlockReason = LFPG_BLOCK_NONE;
            return;
        }

        int nowMs = GetGame().GetTime();
        int elapsed = nowMs - s_LastBlockNotifyMs;

        // Mismo motivo dentro del throttle -> skip
        // Motivo distinto -> notificar inmediatamente (cambio de contexto)
        if (reason == s_LastBlockReason && elapsed < LFPG_PLACEMENT_NOTIFY_THROTTLE_MS)
            return;

        s_LastBlockReason = reason;
        s_LastBlockNotifyMs = nowMs;

        // Usa el helper comun para mantener los mensajes consistentes entre
        // hologram, drop flow y server-side OnPlacementComplete
        string msg = LFPG_GetBlockReasonMsg(reason);
        if (msg == "")
            return;

        float duration = 4.0;
        string title = "#STR_LFPG_MOD_NAME";
        string icon = "set:dayz_gui image:ui_info";
        NotificationSystem.AddNotificationExtended(duration, title, msg, icon);
    }

    static bool CanDeploy()
    {
        if (!s_HasGroup)
            return false;
        return (s_DeployedCount < s_DeployMax);
    }

    // FIX M4: Check de garden limit client-side
    static bool CanPlaceGarden()
    {
        if (!s_HasGroup)
            return false;
        return (s_GardenPlotCount < s_GardenPlotMax);
    }

    // Check greenhouse via whitelist cacheada (para hologram client-side)
    static bool IsGreenhouseCached(EntityAI ent)
    {
        if (!s_EnableGreenhouseAsPlot || !s_EnablePlots)
            return false;

        if (!s_GreenhouseWhitelist || !ent)
            return false;

        int count = s_GreenhouseWhitelist.Count();
        int i;
        for (i = 0; i < count; i = i + 1)
        {
            string ghType = s_GreenhouseWhitelist[i];
            if (ent.IsKindOf(ghType))
                return true;
        }
        return false;
    }

    // v3+: Lista A — placeable sin grupo/zona propia, pero bloqueado en ajena.
    static bool IsNoBaseRequiredCached(EntityAI ent)
    {
        if (!s_NoBaseRequiredTypes || !ent)
            return false;

        int countNBR = s_NoBaseRequiredTypes.Count();
        int iNBR;
        for (iNBR = 0; iNBR < countNBR; iNBR = iNBR + 1)
        {
            string nbrType = s_NoBaseRequiredTypes[iNBR];
            if (ent.IsKindOf(nbrType))
                return true;
        }
        return false;
    }

    // v3+: Lista B — sin restriccion alguna (prioridad sobre lista A).
    static bool IsUnrestrictedCached(EntityAI ent)
    {
        if (!s_UnrestrictedTypes || !ent)
            return false;

        int countU = s_UnrestrictedTypes.Count();
        int iU;
        for (iU = 0; iU < countU; iU = iU + 1)
        {
            string uType = s_UnrestrictedTypes[iU];
            if (ent.IsKindOf(uType))
                return true;
        }
        return false;
    }

    // FIX M-24: Handle cacheado para evitar scan repetido
    static LFPG_FlagBase s_CachedLocalFlag;

    // C1 FIX: Helper centralizado para buscar la bandera del grupo local
    // Usa IsFlagAtPosition (posicion cacheada) en vez de GetGroupID (no sincronizado)
    // Busca alrededor de s_FlagPosition, no del jugador: un radio fijo de 100 m
    // pierde la bandera si el jugador reaparece mas lejos y el radio de obra es mayor.
    // FIX M-24: Cache del handle; se invalida en Clear() y si la flag ya no matchea position.
    static LFPG_FlagBase FindLocalGroupFlag()
    {
        if (!s_HasGroup)
            return null;

        // Cache hit: verificar que el handle sigue valido y en la misma pos
        if (s_CachedLocalFlag && IsFlagAtPosition(s_CachedLocalFlag.GetPosition()))
        {
            return s_CachedLocalFlag;
        }
        s_CachedLocalFlag = null;

        if (!GetGame())
            return null;

        float searchRadius = Math.Sqrt(s_BuildRadiusSq) + 5.0;
        array<Object> objects = new array<Object>;
        array<CargoBase> proxyCargos = new array<CargoBase>;
        GetGame().GetObjectsAtPosition(s_FlagPosition, searchRadius, objects, proxyCargos);

        int i;
        int count = objects.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_FlagBase flag = LFPG_FlagBase.Cast(objects[i]);
            if (flag && IsFlagAtPosition(flag.GetPosition()))
            {
                s_CachedLocalFlag = flag;
                return flag;
            }
        }
        return null;
    }

    // ========================================================================
    // RPC HANDLERS - Llamados desde LFPG_FlagBase.OnRPC o PlayerBase.OnRPC
    // flag puede ser null si el RPC llego via PlayerBase (reconnect sync)
    // ========================================================================
    static void HandleClientRPC(int rpc_type, ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        if (rpc_type == LFPG_RPC_S2C_GROUP_SYNC_FULL)
        {
            HandleGroupSyncFull(ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_S2C_LIGHTWEIGHT_SYNC)
        {
            HandleLightweightSync(ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_S2C_GROUP_DISSOLVED)
        {
            HandleGroupDissolved(ctx);
        }
        else if (rpc_type == LFPG_RPC_S2C_OPEN_NAME_DIALOG)
        {
            HandleOpenNameDialog(ctx, flag);
        }
        else if (rpc_type == LFPG_RPC_S2C_NAME_RESULT)
        {
            HandleNameResult(ctx);
        }
        else if (rpc_type == LFPG_RPC_S2C_ERROR_MSG)
        {
            HandleErrorMsg(ctx);
        }
    }

    protected static void HandleGroupSyncFull(ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string groupID = "";
        if (!ctx.Read(groupID))
            return;

        string groupName = "";
        if (!ctx.Read(groupName))
            return;

        string leaderUID = "";
        if (!ctx.Read(leaderUID))
            return;

        int tier = 1;
        if (!ctx.Read(tier))
            return;

        int deployedCount = 0;
        if (!ctx.Read(deployedCount))
            return;

        int gardenCount = 0;
        if (!ctx.Read(gardenCount))
            return;

        int memberCount = 0;
        if (!ctx.Read(memberCount))
            return;

        // Leer miembros y almacenar para la UI
        if (!s_Members)
        {
            s_Members = new array<ref LFPG_MemberData>;
        }
        s_Members.Clear();

        int i;
        for (i = 0; i < memberCount; i = i + 1)
        {
            string memberUID = "";
            string memberName = "";
            if (!ctx.Read(memberUID))
                return;
            if (!ctx.Read(memberName))
                return;

            bool memberOnline = true;
            ctx.Read(memberOnline);

            LFPG_MemberData md = new LFPG_MemberData();
            md.m_PlayerUID = memberUID;
            md.m_PlayerName = memberName;
            md.m_IsOnline = memberOnline;
            s_Members.Insert(md);
        }

        int deployLimit = 8;
        if (!ctx.Read(deployLimit))
            return;

        vector flagPos = vector.Zero;
        if (!ctx.Read(flagPos))
            return;

        // FIX 4: Build radius sincronizado
        float buildRadiusSq = 900.0;
        ctx.Read(buildRadiusSq);

        // FIX M4: Garden max sincronizado
        int gardenMax = 3;
        ctx.Read(gardenMax);

        // Max group size para UI de miembros
        int maxGroupSize = 6;
        ctx.Read(maxGroupSize);

        // Actualizar cache
        s_GroupID = groupID;
        s_GroupName = groupName;
        s_LeaderUID = leaderUID;
        s_Tier = tier;
        s_DeployedCount = deployedCount;
        s_DeployMax = deployLimit;
        s_GardenPlotCount = gardenCount;
        s_GardenPlotMax = gardenMax;
        s_MemberCount = memberCount;
        s_MaxGroupSize = maxGroupSize;
        s_FlagPosition = flagPos;
        s_BuildRadiusSq = buildRadiusSq;
        s_HasGroup = true;

        // Config flags
        bool enablePlots = true;
        ctx.Read(enablePlots);
        s_EnablePlots = enablePlots;

        bool enableGH = false;
        ctx.Read(enableGH);
        s_EnableGreenhouseAsPlot = enableGH;

        // Greenhouse whitelist
        int ghCount = 0;
        if (!ctx.Read(ghCount))
            ghCount = 0;
        if (!s_GreenhouseWhitelist)
        {
            s_GreenhouseWhitelist = new array<string>;
        }
        s_GreenhouseWhitelist.Clear();
        int gh;
        for (gh = 0; gh < ghCount; gh = gh + 1)
        {
            string ghType = "";
            if (!ctx.Read(ghType))
                break;
            s_GreenhouseWhitelist.Insert(ghType);
        }

        // v3+: NoBaseRequired types (lista A)
        int nbrCountR = 0;
        if (!ctx.Read(nbrCountR))
            nbrCountR = 0;
        if (!s_NoBaseRequiredTypes)
        {
            s_NoBaseRequiredTypes = new array<string>;
        }
        s_NoBaseRequiredTypes.Clear();
        int nbrIR;
        for (nbrIR = 0; nbrIR < nbrCountR; nbrIR = nbrIR + 1)
        {
            string nbrTypeR = "";
            if (!ctx.Read(nbrTypeR))
                break;
            s_NoBaseRequiredTypes.Insert(nbrTypeR);
        }

        // v3+: Unrestricted types (lista B)
        int urCountR = 0;
        if (!ctx.Read(urCountR))
            urCountR = 0;
        if (!s_UnrestrictedTypes)
        {
            s_UnrestrictedTypes = new array<string>;
        }
        s_UnrestrictedTypes.Clear();
        int urIR;
        for (urIR = 0; urIR < urCountR; urIR = urIR + 1)
        {
            string urTypeR = "";
            if (!ctx.Read(urTypeR))
                break;
            s_UnrestrictedTypes.Insert(urTypeR);
        }

        // Deploy item names
        int deployNameCount = 0;
        if (!ctx.Read(deployNameCount))
            deployNameCount = 0;
        if (!s_DeployedItemNames)
        {
            s_DeployedItemNames = new array<string>;
        }
        s_DeployedItemNames.Clear();
        int dn;
        for (dn = 0; dn < deployNameCount; dn = dn + 1)
        {
            string dName = "";
            if (!ctx.Read(dName))
                break;
            s_DeployedItemNames.Insert(dName);
        }

        // Garden item names
        int gardenNameCount = 0;
        if (!ctx.Read(gardenNameCount))
            gardenNameCount = 0;
        if (!s_GardenItemNames)
        {
            s_GardenItemNames = new array<string>;
        }
        s_GardenItemNames.Clear();
        int gn;
        for (gn = 0; gn < gardenNameCount; gn = gn + 1)
        {
            string gName = "";
            if (!ctx.Read(gName))
                break;
            s_GardenItemNames.Insert(gName);
        }

        // Full sync through PlayerBase carries no flag entity and no raise
        // field. A flag already in the bubble still has the synced progress.
        if (flag)
        {
            s_FlagRaiseProgress = flag.m_RaiseProgressNet;
        }
        else
        {
            LFPG_FlagBase streamedFlag = FindLocalGroupFlag();
            if (streamedFlag)
            {
                s_FlagRaiseProgress = streamedFlag.m_RaiseProgressNet;
            }
        }

        // Notificar al panel si esta abierto
        LFPG_GroupPanel panel = LFPG_GroupPanel.GetInstance();
        if (panel)
        {
            panel.OnDataReceived();
        }
    }

    protected static void HandleLightweightSync(ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string groupID = "";
        if (!ctx.Read(groupID))
            return;

        string groupName = "";
        if (!ctx.Read(groupName))
            return;

        string leaderUID = "";
        if (!ctx.Read(leaderUID))
            return;

        int tier = 1;
        if (!ctx.Read(tier))
            return;

        int deployedCount = 0;
        if (!ctx.Read(deployedCount))
            return;

        int deployLimit = 8;
        if (!ctx.Read(deployLimit))
            return;

        vector flagPos = vector.Zero;
        if (!ctx.Read(flagPos))
            return;

        float raiseProgress = 0.0;
        if (!ctx.Read(raiseProgress))
            return;

        // FIX 4: Build radius sincronizado
        float buildRadiusSq = 900.0;
        ctx.Read(buildRadiusSq);

        // FIX AUDIT: Garden count/max en lightweight sync
        int gardenCountLW = 0;
        ctx.Read(gardenCountLW);
        int gardenMaxLW = 3;
        ctx.Read(gardenMaxLW);

        // Member count + max group size
        int memberCountLW = 0;
        ctx.Read(memberCountLW);
        int maxGroupSizeLW = 6;
        ctx.Read(maxGroupSizeLW);

        // FIX I-7: Capturar valores PREVIOS antes de actualizar cache
        int prevDeployCount = s_DeployedCount;
        int prevGardenCount = s_GardenPlotCount;
        bool countsChanged = (prevDeployCount != deployedCount) || (prevGardenCount != gardenCountLW);

        // Actualizar cache
        s_GroupID = groupID;
        s_GroupName = groupName;
        s_LeaderUID = leaderUID;
        s_Tier = tier;
        s_DeployedCount = deployedCount;
        s_DeployMax = deployLimit;
        s_FlagPosition = flagPos;
        s_FlagRaiseProgress = raiseProgress;
        s_BuildRadiusSq = buildRadiusSq;
        s_GardenPlotCount = gardenCountLW;
        s_GardenPlotMax = gardenMaxLW;
        s_MemberCount = memberCountLW;
        s_MaxGroupSize = maxGroupSizeLW;
        s_HasGroup = true;

        bool enablePlotsLW = true;
        ctx.Read(enablePlotsLW);
        s_EnablePlots = enablePlotsLW;

        // Notificar al panel si esta abierto (mismo patron que HandleGroupSyncFull)
        LFPG_GroupPanel panel = LFPG_GroupPanel.GetInstance();
        if (panel)
        {
            panel.OnDataReceived();

            // Pedir full sync con nombres actualizados si el panel esta abierto y hubo delta
            if (countsChanged)
            {
                PlayerBase plr = PlayerBase.Cast(GetGame().GetPlayer());
                if (plr)
                {
                    ScriptRPC rpcReq = new ScriptRPC();
                    rpcReq.Send(plr, LFPG_RPC_C2S_REQUEST_GROUP_DATA, true, null);
                }
            }
        }
    }

    protected static void HandleGroupDissolved(ParamsReadContext ctx)
    {
        string groupID = "";
        ctx.Read(groupID);

        // Solo limpiar si es nuestro grupo
        if (groupID == s_GroupID)
        {
            Clear();
            // Cerrar panel si estaba abierto (evita mostrar datos stale)
            LFPG_GroupPanel.DestroyInstance();
        }
    }

    protected static void HandleOpenNameDialog(ParamsReadContext ctx, LFPG_FlagBase flag)
    {
        string groupID = "";
        ctx.Read(groupID);

        // Abrir el dialogo de nombre con referencia a la bandera para RPCs
        LFPG_GroupNameDialog.Open(groupID, flag);
    }

    protected static void HandleNameResult(ParamsReadContext ctx)
    {
        int result = 0;
        ctx.Read(result);

        LFPG_GroupNameDialog.HandleNameResult(result);
    }

    protected static void HandleErrorMsg(ParamsReadContext ctx)
    {
        string msg = "";
        ctx.Read(msg);

        // Mostrar notificacion al jugador via NotificationSystem vanilla
        // FIX M-5: icono default para mejor UX
        float duration = 5.0;
        string title = "#STR_LFPG_MOD_NAME";
        string icon = "set:dayz_gui image:ui_info";
        NotificationSystem.AddNotificationExtended(duration, title, msg, icon);
    }
};
