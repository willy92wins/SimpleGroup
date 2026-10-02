# Estado de las issues de producto — 2026-10-02

Referencia de entrada: main `85e2ce0`, con #1, #5–#9 y #16 fusionados.
Esta matriz usa exclusivamente F01–F33 de la lane L6 publicada en [#10](https://github.com/willy92wins/SimpleGroup/issues/10).
Las decisiones del dueño están en [PRODUCT-DECISIONS.md](PRODUCT-DECISIONS.md);
las ejecuciones y sus límites, en [PRODUCT-VALIDATION.md](PRODUCT-VALIDATION.md).
Una corrección de código no equivale a aceptación completa en producción.

## #10: resolución de la auditoría

| Claim | Disposición actual | Ancla de producto / pendiente |
|---|---|---|
| F01 | Regla resuelta en #15; revalidación corregida | `LFPG_ActionRaiseFlag` / `LFPG_ActionLowerFlag`: cualquiera puede actuar, con distancia, estado, tier y energía aplicables. |
| F02 | Corregido | `LoadGroups` / `SaveGroups`: carga fallida entra en solo lectura; no sobrescribe el perfil. |
| F03 | Corregido | `Init` / `RunBootAudit`: las banderas restauradas conservan su identidad cuando falta/falla el perfil. |
| F04 | Corregido | `SaveGroups`: valida el final antes de rotar backup; recuperación e I/O denegado ejercitados. |
| F05 | Corregida la resurrección desde tmp vacío; límite documentado | Cero grupos es válido. CopyFile/DeleteFile siguen sin promesa de atomicidad ante interrupción física. |
| F06 | Transporte corregido; aceptación parcial | Full/lightweight sync a miembros usa PlayerBase. Invitaciones, transferencia y sync con dos clientes siguen pendientes. El diálogo automático sigue ligado a bandera cercana; la recuperación desde panel por PlayerBase pasó en motor a más de 1,5 km. |
| F07 | Refresco implementado; aceptación CE pendiente | `ApplyGroupLifetime` / `RefreshRaisedBases`: comprobar types.xml, reinicios y vida efectiva del servidor de destino. |
| F08 | Guards de carga/shutdown/identidad corregidos | `LFPG_FlagBase.EEDelete`: borrar la bandera real sigue disolviendo el grupo; no se promete supervivencia ante wipe administrativo. |
| F09 | Corregido | `LFPG_FlagKit_T1.OnPlacementComplete`: revalida y conserva el kit cuando falla la creación. |
| F10 | Corregido | Consultas de territorio leen progreso vivo; ya no dependen de un progreso congelado al registrar. |
| F11 | Corregido | Reaplica configuración a banderas restauradas tras cargar config. |
| F12 | Corregido | `LFPG_CountsAsFurniture`: colocación, drop y recuento comparten filtros de muebles y listas A/B. |
| F13 | Corregido | `RegisterFlag` recupera el tier de la entidad restaurada. |
| F14 | Decisión aplicada y validada en motor | Cancelación conserva grupo/bandera; finalización única tras 5447 ms, con revalidación al completar. Salida/kick/transfer conservan confirmación. |
| F15 | Corregido | Los RPC RESERVED no mutan; se retiraron sus handlers inseguros. |
| F16 | Decisión aplicada y validada en motor | El líder de un grupo temporal puede reabrir el nombre desde el panel. Petición y ACK por PlayerBase, ligados al grupo actual. No permite renombrar nombres definitivos. |
| F17 | Decisión implementada | Exclusiones eximen grupo/zona/cupo; blacklist prevalece. Excepciones sin actor documentadas. |
| F18 | Corregido | `CanBePlaced` no disuelve grupos ni guarda datos. |
| F19 | Corrección aplicada; retorno y cupo probados en motor | Retorno SERVER diferido fuera del callback; no es veto atómico y puede fallar si desaparece el destino. Último cupo contado una vez y retorno del exceso comprobados con objetos y movimientos reales. |
| F20 | Corregido | Límites de nombre coherentes 1..48 y compartidos con cliente. |
| F21 | Corregido | Config inválida se conserva; defaults solo en memoria. Valores de lifetime legacy probados antes/después del fix. |
| F22 | Corregido y validado en motor | Misma fixture: 26 checks/3 fallos antes y 26/0 después. Tres JSON supervivientes idénticos por bytes y registros completos esperados. IDs/UIDs/líder cubiertos también por bb7408de. Conserva grupos legacy, sin expulsar miembros por reducir maxMembers ni renombrar datos antiguos. |
| F23 | Corregido | Retira timer anterior al activar/desactivar invitación. |
| F24 | Corregido | Iteración de destinatarios con buffer separado del usado al construir sync. |
| F25 | Guard corregido y probado en motor | `UpgradeFlag` rechaza la segunda llamada en el mismo tick con la referencia anterior y conserva T2 registrada. No se afirma concurrencia de dos clientes. |
| F26 | Corregido | Trim y clave sin distinción de mayúsculas para nombres nuevos. |
| F27 | Guardado medido y optimizado; aceptación de carga parcial | 120 identidades: cinco guardados pasan de 3842–4604 ms a 125–191 ms en DayZDiag. Recuento con objetos reales en aceptación; no equivale a 120 clientes conectados. |
| F28 | Inventario cerrado; contratos conservados por compatibilidad | Ver [F28-COMPATIBILITY.md](F28-COMPATIBILITY.md). Handlers inseguros retirados; campos persistidos, símbolos públicos y firmas se conservan conscientemente. |
| F29 | Corregido | Delta de reloj unsigned y sincronización terminal a cero. No se simulan 49 días de uptime como prueba ejecutada. |
| F30 | Refutado por la auditoría | Manager creado antes de restaurar entidades; no se parchea una ruta que no se ha demostrado. |
| F31 | Resuelto por decisión del dueño | Conservar sucesión al salir o expulsar; no implementar sucesión por inactividad. |
| F32 | Refutado en la ruta descrita | La ventana de config durante restauración se trata en F11. |
| F33 | Corregido | Throttle por tipo y feedback; Leave conserva caché hasta confirmación del servidor. |

La #10 permanece abierta como lista de aceptación pendiente y deuda residual;
no debe seguir presentando los defectos corregidos como trabajo por implementar.
PRs de continuación propuestos, sin cambiar reglas de juego por inferencia:

1. Aceptación multicliente: invitación, transferencia, permisos y sync fuera de
   burbuja. Aplazada expresamente por falta de segundo cliente.
2. Aceptación CE y rendimiento en el servidor de destino, más interacción
   física/inspección visual; cada dimensión requiere su evidencia propia.
3. F28 resuelta por inventario y conservación explícita de compatibilidad; no
   retirar JoinTimestamp ni contratos públicos solo por no tener lectores internos.

Primer run de aceptación `02b4654c-320c-466a-85c4-9b1dc1ac4763` sobre main:
UI 28 comprobaciones/0 fallos de callbacks y respuesta RPC; acciones/inventario
38 comprobaciones/3 fallos (colocación exenta, último cupo y retorno por exceso).
Esos resultados no se convierten retroactivamente en PASS. La repetición final
`de49e59c-7d94-4205-9610-8bd183e0f32b` comprueba sitio limpio y movimientos
reales: acciones 57/0 y UI 44/0, incluidos preparación, F14/F16 y el guard F25.
Opus 5.5 aprobó el código; r2 sobre el RPC truncado fue MERGE_OK_WITH_LIMIT:
se rechaza sin mutación y el motor registra una excepción de lectura. La
limitación y los fallos de fixture previos están en PRODUCT-VALIDATION.md.

## #14: v2 implementada y validada

El dueño levantó expresamente el aplazamiento el2026-10-03: implementar v2 ahora.
Se implementa envelope con contador y checksum del payload UTF-8 exacto, lectura
legacy sin reescritura, copias pre-v2 numeradas y exportador v1 que conserva los
datos posteriores a la migración. Contrato y rollback en [GROUPS-FORMAT.md](GROUPS-FORMAT.md).

Gauntlet Opus5.5: r1 detectó bloqueo permanente tras fallo transitorio de lectura;
r2 detectó bloqueo del reintento tras copia parcial. Ambos corregidos; r3
MERGE_OK_STATIC. Los límites nativos de strings se aislaron y corrigieron con
conciliación de Codex posterior a r3, autorizada por el dueño. Opus revisó también
el nuevo delta de rendimiento sin hallazgos. Exportador Python: diez tests PASS.
DayZDiag: 124 comprobaciones y cero fallos, incluidas migración, integridad,
recuperación, copia parcial y denegación real de lectura. El binario anterior
cargó los 120 grupos exportados y conservó los cambios posteriores a v2.
Evidencia y límites en [PRODUCT-VALIDATION.md](PRODUCT-VALIDATION.md).

Objetivo nuevo de #10: soporte multimapa con perfiles/CE separados,100–120players.
Se incluye plantilla CE en [server/README.md](server/README.md). La medición de
120 identidades sintéticas no acredita120 clientes ni un servidor destino que
todavía no se ha identificado.
