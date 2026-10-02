# F28: inventario y disposición de compatibilidad

Base examinada: c8e803c; revisión 2026-10-03. F28 no exige romper contratos para
reducir el número de declaraciones. Se cierra el inventario con esta disposición.

| Superficie | Consumidor verificado | Disposición |
|---|---|---|
| Handlers C2S RESERVED | No hay emisor legítimo; retirados en los PR anteriores | Resuelto; no se reintroducen |
| S2C_GROUP_SYNC_UPDATE, S2C_DEPLOY_DENIED | Sin emisor/handler interno | Conservar IDs como reserva de protocolo, sin reutilizarlos |
| LFPG_RPC_THROTTLE_MS | Sin lector interno; manda config.m_RpcThrottleMs | Conservar símbolo público legacy; no usarlo como configuración efectiva |
| updateType en SendGroupSyncUpdateToMembers | Cinco llamadas; cuerpo envía full sync | Conservar firma protegida para overrides de mods; no activar protocolo parcial |
| GroupData.m_FlagNetLow/High | Constructor y escrituras en create/upgrade; sin lector interno | Conservar campos públicos runtime y sus valores; no pasan a JSON |
| MemberData.m_JoinTimestamp | Se escribe en Set y se serializa en cada miembro | Conservar bytes/valor legacy en v1, payload v2 y rollback. No es reloj absoluto ni criterio de sucesión |
| FlagPositionCache.m_Tier | Se escribe al construir/actualizar; sin lector interno | Conservar estructura pública y firma Set; no coste de red/persistencia |
| IsTerritoryFlag | Contrato público explícito FIX M-13 | Conservar |
| IsFullyRaised, IsPlayerInBuildZone, DebugPrintState | Sin llamador interno de producto | Conservar consultas/diagnóstico públicos; no ejecutan trabajo periódico por existir |
| m_RPCThrottle | IsRPCThrottled; PruneRPCThrottle elimina caducados al superar512 | La claim histórica «nunca se poda» ya no describe la base actual |
| LFPG_DeployTracker.ClearAll en cliente | Limpieza de MissionGameplay | Mantener limpieza idempotente; no eliminar por asumir para siempre que cliente estará vacío |

Fuentes: `scripts/3_Game/LFPG_TerritoryEnums.c`, `LFPG_GroupData.c`,
`scripts/4_World/managers/LFPG_GroupManager.c` (SendGroupSyncUpdateToMembers,
CreateGroup, UpgradeFlag, IsRPCThrottled, PruneRPCThrottle),
`scripts/4_World/entities/LFPG_FlagBase.c` y MissionGameplay.

El barrido de símbolos en los checkouts locales de Repos y DayZ Projects devolvió
54 archivos, todos copias de SimpleGroup. Hubo directorios de dependencias Python
inaccesibles; no son prueba de ausencia universal de consumidores. El código de
LFPowerGrid disponible no usa estos símbolos exactos. Los mods externos no
disponibles siguen siendo una razón para conservar contratos públicos, no una
afirmación de que exista un consumidor observado.

No se cambian esquema de miembro, layout de RPC, IDs ni semántica de sucesión.
No hay una optimización de rendimiento atribuible a borrar estas declaraciones.
Si se decide una ruptura de API futura, se hará versionada y con inventario de
los mods realmente instalados. F27 conserva su medición independiente.
