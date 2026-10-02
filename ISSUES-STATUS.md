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
| F06 | Transporte corregido; aceptación parcial | Full/lightweight sync a miembros usa PlayerBase. Entrega fuera de burbuja, invitaciones y transferencia con dos clientes siguen pendientes. El diálogo automático sigue ligado a bandera cercana; F16 añade recuperación desde el panel por PlayerBase, pendiente de aceptación nativa. |
| F07 | Refresco implementado; aceptación CE pendiente | `ApplyGroupLifetime` / `RefreshRaisedBases`: comprobar types.xml, reinicios y vida efectiva del servidor de destino. |
| F08 | Guards de carga/shutdown/identidad corregidos | `LFPG_FlagBase.EEDelete`: borrar la bandera real sigue disolviendo el grupo; no se promete supervivencia ante wipe administrativo. |
| F09 | Corregido | `LFPG_FlagKit_T1.OnPlacementComplete`: revalida y conserva el kit cuando falla la creación. |
| F10 | Corregido | Consultas de territorio leen progreso vivo; ya no dependen de un progreso congelado al registrar. |
| F11 | Corregido | Reaplica configuración a banderas restauradas tras cargar config. |
| F12 | Corregido | `LFPG_CountsAsFurniture`: colocación, drop y recuento comparten filtros de muebles y listas A/B. |
| F13 | Corregido | `RegisterFlag` recupera el tier de la entidad restaurada. |
| F14 | Decisión aplicada; aceptación nativa pendiente | Destroy usa acción continua de 5 segundos con revalidación al completar. Salida/kick/transfer conservan confirmación. |
| F15 | Corregido | Los RPC RESERVED no mutan; se retiraron sus handlers inseguros. |
| F16 | Decisión aplicada; aceptación nativa pendiente | El líder de un grupo temporal puede reabrir el nombre desde el panel. Petición y ACK por PlayerBase, ligados al grupo actual. No permite renombrar nombres definitivos. |
| F17 | Decisión implementada | Exclusiones eximen grupo/zona/cupo; blacklist prevalece. Excepciones sin actor documentadas. |
| F18 | Corregido | `CanBePlaced` no disuelve grupos ni guarda datos. |
| F19 | Corrección aplicada; aceptación de inventario en curso | Retorno SERVER diferido fuera del callback; no es veto atómico y puede fallar si desaparece el destino. El caso de último cupo se sigue por separado en la misma aceptación. |
| F20 | Corregido | Límites de nombre coherentes 1..48 y compartidos con cliente. |
| F21 | Corregido | Config inválida se conserva; defaults solo en memoria. Valores de lifetime legacy probados antes/después del fix. |
| F22 | Corregido y validado en motor | Misma fixture: 26 checks/3 fallos antes y 26/0 después. Tres JSON supervivientes idénticos por bytes y registros completos esperados. IDs/UIDs/líder cubiertos también por bb7408de. Conserva grupos legacy, sin expulsar miembros por reducir maxMembers ni renombrar datos antiguos. |
| F23 | Corregido | Retira timer anterior al activar/desactivar invitación. |
| F24 | Corregido | Iteración de destinatarios con buffer separado del usado al construir sync. |
| F25 | Guard corregido; aceptación de doble finalización pendiente | `UpgradeFlag` exige que oldFlag sea la entidad registrada. No se afirma una prueba de concurrencia del motor. |
| F26 | Corregido | Trim y clave sin distinción de mayúsculas para nombres nuevos. |
| F27 | Coste no medido; propuesta de medición | Medir guardado, recuento y consultas con población/objetos representativos antes de proponer optimización. |
| F28 | Riesgo de handlers retirado; limpieza pendiente propuesta | Constantes RPC, campos NetLow/High, JoinTimestamp y parámetro updateType se mantienen hasta comprobar consumidores externos y compatibilidad de persistencia; destino en la propuesta 4. |
| F29 | Corregido | Delta de reloj unsigned y sincronización terminal a cero. No se simulan 49 días de uptime como prueba ejecutada. |
| F30 | Refutado por la auditoría | Manager creado antes de restaurar entidades; no se parchea una ruta que no se ha demostrado. |
| F31 | Resuelto por decisión del dueño | Conservar sucesión al salir o expulsar; no implementar sucesión por inactividad. |
| F32 | Refutado en la ruta descrita | La ventana de config durante restauración se trata en F11. |
| F33 | Corregido | Throttle por tipo y feedback; Leave conserva caché hasta confirmación del servidor. |

La #10 permanece abierta como lista de aceptación y decisiones residuales;
no debe seguir presentando los defectos corregidos como trabajo por implementar.
PRs de continuación propuestos, sin cambiar reglas de juego por inferencia:

1. Aceptación con un cliente: acciones, doble finalización de upgrade (F25),
   inventario, último cupo y UI; corregir solo
   defectos reproducidos. Adjuntar resultados y separar fallos de fixture.
2. Aceptación multicliente, CE y rendimiento: cada dimensión requiere su evidencia
   propia. Multicliente está aplazado expresamente por falta de segundo cliente.
3. Aceptar las decisiones F14/F16: mantener Destroy 5 segundos y recuperar
   nombre temporal desde el panel. F31 queda resuelto sin cambio de código.
4. Limpieza F28: inventariar consumidores de constantes/contratos/argumentos y
   campos; retirar únicamente lo que se demuestre sin uso y compatible con
   perfiles legacy. JoinTimestamp forma parte del JSON: no tratar su retirada
   como si fuera solo un campo privado sin persistencia.

Primer run de aceptación `02b4654c-320c-466a-85c4-9b1dc1ac4763` sobre main:
UI 28 comprobaciones/0 fallos de callbacks y respuesta RPC; acciones/inventario
38 comprobaciones/3 fallos (colocación exenta, último cupo y retorno por exceso).
Se están discriminando precondiciones y movimiento asíncrono del fixture;
estos tres resultados no se atribuyen aún al producto ni se presentan como PASS.

## #14: propuesta para un PR futuro de groups.json v2

Estado: **aplazado**, tal como exige [#14](https://github.com/willy92wins/SimpleGroup/issues/14),
hasta observar estabilidad de #11/#12/#13 en producción. Una suite local verde
no satisface esa condición. Este documento no activa una migración.

La premisa original necesita precisión: el constructor actual de
`LFPG_GroupsFileData` usa versión 0 y grupos null como centinelas;
`ValidateGroupsData` rechaza campos obligatorios ausentes. Un archivo v1 con
versión explícita y array vacío es válido. V1 todavía carece de un contador
esperado y digest que permitan detectar una alteración que siga siendo un
payload válido pero haya perdido registros. No se promete que v2 resuelva
atomicidad, cortes de energía o edición maliciosa.

Propuesta [DESIGN], previa a elegir y verificar las APIs de implementación:

- Lectura de v1 sin reescritura durante el arranque; validación integral antes
  de instalar grupos. La primera escritura v2 sucede solo por una mutación
  legítima después de aceptar la migración.
- Versionado explícito, número esperado de grupos y digest sobre una
  representación canónica especificada. Orden de grupos/miembros, cadenas,
  números, codificación y exclusión del propio digest deben quedar definidos
  con vectores de prueba independientes antes de escribir código. Elegir el
  algoritmo solo tras verificar su disponibilidad real en DayZ.
- Política final/tmp/bak explícita y compatible con la actual «gana final
  válido»; no introducir una supuesta secuencia de commits dentro del digest.
- Fallo de parseo, contador, digest o versión futura: conservar todos los
  candidatos y prohibir mutaciones; nunca reemplazar por estado vacío.
- Backup v1 verificable antes de la primera escritura v2. Para volver a una
  versión antigua después de nuevas mutaciones, conservar el v2 completo y
  disponer de una exportación v2→v1 validada con los datos más recientes.
  Restaurar solo el backup inicial puede perder cambios: no es rollback sin
  pérdida y requiere una decisión explícita del administrador.

Criterios del futuro PR: cero grupos legítimos; campos obligatorios ausentes;
registro omitido con JSON aún válido; digest incorrecto; reordenación según
la canonización; v1 sin cambios al leer; primer guardado v2; versión futura;
I/O denegado en cada candidato; reinicio entre pasos de escritura; exportación
de rollback que conserve grupos, líderes, miembros y nombres actuales.

Para levantar el aplazamiento se necesitan build/config identificados,
observaciones del servidor de destino sobre carga/guardado y reinicios, y
decisión del dueño sobre suficiencia de esa observación. No se inventa un
plazo ni un volumen de producción ya cumplidos. #14 solo se cerrará al
implementar y validar el formato o al descartarlo explícitamente.
