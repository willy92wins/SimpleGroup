# Validación del candidato

## Release 1.0.0-rc1 — matriz nativa y smoke retail (2026-10-04)

Matriz de reproducción en servidor DayZDiag 1.29, resumen v2 por escenario: estado (fallos/comprobaciones).
A = `main` `13d7b80`; B = `4a2f52c` (arreglos de la auditoría, correcciones de la re-auditoría y empaquetado);
C = `c70237b` (B + SG-02), repetido solo en los escenarios de persistencia.

| Escenario | Qué comprueba | A | B | C |
|---|---|---|---|---|
| S1 | SG-11 sucesión del líder | esperado (1/6) | esperado (0/6) | esperado (0/6) |
| S2 | SG-03 groups.json sin la lista de grupos | esperado (2/2) | esperado (0/2) | esperado (0/2) |
| S2c | control de S2 | esperado (0/2) | esperado (0/2) | esperado (0/2) |
| S3 | SG-01 corte en la ventana de guardado (medida) | medido (0/0) | medido (0/0) | medido (0/0) |
| S4 | SG-01 `.bak` bloqueado | esperado (1/5) | esperado (0/5) | esperado (0/5) |
| S5 | SG-02 tmp lleno de NUL | medido (2/2) | inesperado (2/2) | esperado (0/2) |
| S6 | SG-04 tmp inválido sin final | inconcluso (2/2) | inconcluso (0/2) | inconcluso (0/2) |
| S7 | SG-17/SG-18 listas omitidas y límites | esperado (4/5) | esperado (0/5) | — |
| S7c | control de S7 | esperado (0/2) | esperado (0/2) | — |
| S8 | SG-12 banderas frente a explosiones y munición | esperado (2/6) | esperado (0/6) | — |
| S11 | SG-20 explosivos excluidos | esperado (3/9) | esperado (0/9) | — |
| S9-1 | SG-06 vida de una bandera bajada (medida) | medido (0/0) | medido (0/0) | — |
| S9-2 | SG-06 tras reiniciar (medida) | medido (0/0) | medido (0/0) | — |

- En A, «esperado» con fallos significa que el defecto se reproduce como predijo la auditoría; en B y C, que el
  arreglo se comporta como se esperaba.
- S5 en A mide el defecto: con el tmp lleno de NUL el servidor queda en solo lectura y no carga el grupo del
  final. En B sale «inesperado» porque B no lleva el arreglo de SG-02; en C carga el grupo, queda escribible y el
  tmp se aparta con sus 4096 bytes.
- S6 sale «inconcluso» en todas las builds por un `JSON ERROR` que el motor escribe al leer el tmp inválido que
  prepara el propio escenario; sus comprobaciones coinciden con lo esperado en cada build.
- S9: la vida máxima de 45 días de una bandera bajada sobrevive al reinicio; SG-06 no se reproduce en motor.

Smoke en DayZServer retail 1.29 (`verifySignatures = 2`, BattlEye, Dabs Framework y el paquete firmado con
`Return0`; la misión crea el kit y las tres banderas, las borra y pide el cierre):

- paquete de `4a2f52c`: PASS; `SCRIPT (E)` 0, errores del mod 0, `config.json` creado sí, `allowDamage` de las banderas Flag_T1 0, Flag_T2 0, Flag_T3 0, cierre pedido por la misión sí.
- paquete de `c70237b`: PASS; `SCRIPT (E)` 0, errores del mod 0, `config.json` creado sí, `allowDamage` de las banderas Flag_T1 0, Flag_T2 0, Flag_T3 0, cierre pedido por la misión sí.

No cubierto aquí: el aspecto en juego de los materiales de los mástiles y la conexión de un cliente retail
(los cubre `TESTERS.md`).


## Configuración y CE — PR #19

Código `740215c`, PBO SHA256
`f3988e61d30a4f9f6957858685f564babbca55bc844739b095423a0e360f51cb`.
42 scripts/64 recursos extraídos idénticos; lint cero errores y 17 avisos de la
base. Opus 5.5 r1 pidió conservar los valores cargados ante fallo del inspector
de claves y acotar el coste de lectura. Ambos ajustes aplicados; r2 revisó solo
el delta y emitió `MERGE_OK_STATIC`, sin hallazgos nuevos.

La corrección diferencia listas omitidas y listas vacías en configuraciones
antiguas. La prueba aislada confirmó que el motor convierte ambas en arrays
vacíos, incluso inicializando el destino a null; por eso se inspeccionan las
claves de primer nivel del mismo JSON validado. No se reescribe el archivo.

- Chernarus, run `a35e3cb2-1e02-4cee-8c0a-74248f1f1f08`: 253 comprobaciones,
  cero fallos. Quince configuraciones: v2/v3/v4 omitidas/vacías/custom, v5
  vacías/custom, claves escapadas y señuelos anidados/en strings, más un archivo
  formateado con 450 tipos. Se conservan maxGroup=7, radio=45, lifetime=1234,
  listas personalizadas y los bytes del archivo.
- Las 24 banderas T1/T2/T3 conservan identidad, tier, progreso y lifetime tras
  reinicio. Recuento exacto de 50 cajas por base, 1200 en total: 483 ms para las
  24 búsquedas. 20000 consultas positivas/negativas de zona: 67 ms.
- Enoch, run `5c4c651c-daba-4edc-992d-077990184c86`: 326 comprobaciones, cero
  fallos al crear las mismas 24 banderas/1200 cajas con perfil y CE independientes.
  Recuento: 390 ms; 20000 consultas de zona: 67 ms. Tras cierre ordenado,
  run `aba643d1-2188-48cd-91df-1f762e37b520`: 253 comprobaciones, cero fallos,
  las mismas 24 banderas y 1200 cajas restauradas. Recuento: 798 ms; 20000
  consultas: 106 ms. La variación entre runs queda registrada, sin extrapolar FPS.

Los ensayos previos contaban cero con los mismos objetos y la configuración
antigua; se conservan como fallidos. La repetición previa al ajuste de revisión
dio 231/0. Tres inputs lenientes no reprodujeron una aceptación del parser con
rechazo del inspector: esa rama defensiva está revisada estáticamente y no se
declara ejercitada en motor. Los tiempos corresponden a estrés sintético en
DayZDiag; no acreditan 120 clientes conectados ni rendimiento del destino.
El recuento global medido es síncrono y todavía puede producir una pausa; medir
el hardware/configuración de destino antes de certificar esa carga.

Evidencia local en `acceptance/native-config-final-chernarus`,
`acceptance/native-enoch-create` y `acceptance/final-enoch`, bajo el directorio
de trabajo indicado abajo. PBO y nueve archivos originales del perfil se
restauran por hash al terminar; no se despliega esta fixture en producción.

## Aceptación de v2 — 2026-10-03

Código probado: `d2f8b8c`, basado en main `c8e803c`. PBO SHA256
`6d80d50a440b9f4840320c316a63004db0fe08fa99f710da165508d297827de6`:
42 scripts y 64 recursos extraídos idénticos; cuatro modelos ODOL. Compilación
y ejecución en DayZDiag dedicado. Lint: cero errores, 17 avisos de la base.
Exportador Python 3.14: diez tests positivos/negativos, fuentes intactas.

Gauntlet: Codex implementó; Opus 5.5 revisó persistencia hasta r3, con r2+ solo
delta. Sus dos fallos de reintento quedaron corregidos. Codex concilió después
los límites nativos de strings bajo la autorización del dueño desde r3. Opus
revisó separadamente el nuevo delta de rendimiento (`8a56295..d2f8b8c`) y emitió
`MERGE_OK_STATIC`; no equivale por sí solo a aceptación nativa.

- Run `485c82a3-4c6a-41ac-bf28-39bf129242b7`: 124 comprobaciones, cero fallos,
  incluyendo preparación. V1 leído sin reescritura, apagado limpio sin migrar,
  primera y segunda migración con backups exactos, cero grupos, registro omitido
  manteniendo JSON válido, contador/checksum incorrectos, futuro en cada
  candidato, final corrupto, tmp incompleto, recuperación sin final, copia
  parcial y reintento con una mutación posterior.
- Denegación real de lectura mediante un handle exclusivo del host: dirty
  conservado; al liberar el handle se guardó la mutación actual. La copia
  parcial se inyectó en el verificador de la fixture, sin modificar producto.
- Python/zlib verificó los archivos escritos por DayZ y exportó 120 grupos.
  El servidor cargó además un envelope creado independientemente por Python,
  con Unicode, un nombre de 525 bytes y coordenadas negativas/grandes.
- Run `bfd4ff13-ee31-4287-bfbb-1cfc1023a836`: PBO anterior `e0111d3`, cuyo código
  de producto coincide con `c8e803c`, cargó la exportación v1. Los 120 grupos
  coincidieron en todos los campos persistidos y conservaron el cambio posterior
  al bloqueo de lectura. No se restauró el backup inicial como rollback.

### Rendimiento medido

Misma fixture, cinco guardados síncronos por escenario, DayZDiag local:

| Datos sintéticos | Carga optimizada | Guardado antes | Guardado optimizado |
| --- | ---: | ---: | ---: |
| 24 grupos, 5 miembros cada uno | 18 ms | 773–923 ms | 61–103 ms |
| 120 grupos, 1 miembro cada uno | 46 ms | 3842–4604 ms | 125–191 ms |

Los fallos iniciales se conservan como evidencia: JsonSerializer recorta strings
a 1023 bytes; SubstringUtf8 devuelve vacío desde offset 8192. Experimentos
aislados confirmaron ambas fronteras. La representación final divide sin cortar
caracteres y el checksum procesa bloques de bytes pequeños. Una comparación
aislada del mismo texto midió 550–655 ms frente a 14–23 ms para el checksum,
con resultados idénticos. No se eliminó ninguna validación para reducir el coste.

Los datos anteriores representan 120 identidades, no 120 clientes conectados.
El coste síncrono del guardado todavía puede afectar un frame; no certifica el
hardware ni el conjunto de mods del servidor de destino.

F28: inventario y conservación explícita de contratos públicos/persistidos,
revisados por Opus. Plantilla CE de cuatro clases y guía de perfiles separados
por mapa incluidas. Chernarus: 24 banderas T1/T2/T3 creadas con lifetime inicial
604800 y registrado 3888000; las 24 conservaron identidad, tier y progreso tras
cierre ordenado y reinicio. Los tres vectores independientes con caracteres de
2/3/4 bytes atravesando la frontera 512, longitud total 1024, dieron PASS.

El ensayo anterior al PR #19 falló: la búsqueda espacial encontraba las 50
cajas esperadas en la primera base, pero la configuración v3 cargada deja vacía
la nueva lista de muebles que no existía en ese JSON. Las 24 bases contaban cero.
Runs `c7936dcc-9886-4b0b-954e-7f63b98050da` y
`159cf678-3ed1-4ca0-8a26-49631ab3b9ca`; archivos originales intactos. Este hallazgo
de migración de configuración originó la corrección del PR #19. No invalida
las pruebas de v2 ni convierte aquel recuento fallido en aceptación de carga.

Evidencia: expediente local del dueño (no publicado), directorios
`native-v2-optimized`, `native-rollback-old-reader`, `native-string-probe` y
`native-perf-probe`, con sus revisiones.
Los primeros ensayos fallidos no se presentan como PASS. El diagnóstico del
plugin vanilla `PluginItemDiagnostic` al iniciar DayZDiag sigue siendo visible;
no se afirma ausencia absoluta de errores del entorno.

## Histórico 2026-10-02 (código anterior a v2)

Base de las suites de grupos/persistencia: `11ba729bd03c6f5eb26d2ef1e58b9f8d52672290`.
Último código: `e0111d3c38ebbaaa255bd01ea13a36b232341c02` (F22 y decisiones F14/F16).
Estado: integración revisada y fusión autorizada; aceptación de producción pendiente.

## Revisión de producto

Gauntlet con Claude Opus 5.5 (`claude-opus-5-5`), contextos independientes.
Desde r2 se revisaron exclusivamente cambios y cierre de hallazgos.
Acciones: r2 OK. Territorio, persistencia, UI/sync y F17: r3 OK.
El guard de colisión de ID se concilió por Codex según la autorización desde r3.

Revisión final de integración de #1, #5, #6, #7, #8, #9 y #16: Opus r1 encontró
que la migración a config v5 sobrescribía el ajuste explícito de lifetime en
archivos antiguos. Codex lo reprodujo en motor y corrigió en a8d4a01; Opus r2,
limitado a ese delta, emitió **MERGE_OK**. Todos los heads originales son
ancestros reales de la integración; se conserva su historial al fusionar.

## Comprobaciones ejecutadas

- Validador: 0 errores, 17 avisos iguales a la base, sin avisos nuevos.
- UI: 3 layouts, 41 fuentes y 49 claves; 0 fallos y 0 avisos de reconciliación.
- PBO a8d4a01 extraído: 41 scripts y 62 recursos coinciden con las fuentes;
  config.bin y cuatro modelos ODOL presentes. SHA-256:
  `9aaf1ebad8f02984f18ed76dbc02070cc173b5533cc61656bfbd53b44f85b4da`.
- En 11ba729, DayZ 1.29.163709 sin file patching: servidor y cliente compilan
  y cargan el mod. La repetición de a8d4a01 descrita abajo ejecuta el servidor.
- Misión aislada, run `ac54e426-ba46-4f00-a352-a123244f98e6`:
  **50 comprobaciones nativas, 0 fallos**. Cubre grupo/líder/miembros,
  nombres, bandera duplicada, guardado y relectura de JSON, cero grupos,
  exenciones F17 sin grupo/en territorio ajeno/con cupo lleno, prioridad de
  blacklist, política válida/vacía/truncada, marcas ausentes o incorrectas,
  tamaños inválidos y conservación de reglas al salir de un grupo.
- La prueba aisló un fallo real: el serializador puede devolver true/0 al
  leer un entero ausente. El nuevo mensaje de política exige una marca final
  antes de instalar ninguna lista; la repetición verifica la corrección.
- Sesión cerrada ordenadamente. PBO y nueve archivos originales del perfil
  restaurados y verificados por hash; no es un despliegue de producción.
- Persistencia, run `bb7408de-bd00-404f-a04f-6c619ea3c10d`: **64 aserciones
  de producto nativas y 24 comprobaciones independientes de archivos, 0 fallos**.
  La misión invoca Init/LoadGroups/SaveGroups originales; un adaptador permite
  observar índices, dirty y modo de solo lectura, sin sustituir esos métodos.
  Cubre JSON válido/vacío/corrupto, backup, tmp pendiente, IDs/miembros duplicados,
  líder ausente, versiones futuras y bandera restaurada con perfil perdido.
- Se niega realmente escritura/borrado mediante handles de Windows en tmp,
  backup y final, con un control independiente que confirma cada denegación.
  Los tres guardados fallan conservando dirty y datos recuperables. Al liberar
  el handle, el reintento guarda el estado más reciente y limpia dirty/tmp.
  Los archivos se comparan por bytes y se verifica el contenido final/backup.
  No equivale a una prueba de corte de alimentación ni de caída del proceso.
  Perfil y PBO originales restaurados por hash tras el cierre ordenado.
- Config, misma fixture antes y después: run `5417e77b-5998-4b37-b541-f6697e67e850`
  con 11ba729 reproduce ocho fallos de valor; run
  `cff55c57-5e38-4064-869b-60eaaf47213d` con a8d4a01 termina **30 checks,
  cero fallos** (24 aserciones de producto y seis de preparación).
  Cubre v4 con -1/0/3600, v3 con -1, v5 con -1 y clave ausente = 86400,
  con dos cargas de cada caso. Doce archivos guardados como evidencia coinciden
  byte a byte con las entradas originales. Se conserva el JSON del admin.
  Cierre ordenado y restauración de PBO/perfil comprobados por hash.

## Límites de la evidencia

El parser cliente se ejercitó con serializadores nativos en el proceso servidor;
eso no prueba el transporte RPC a varios clientes. La reconciliación de layouts
no prueba interacción visual. Las acciones, inventario y callbacks/UI se
ejercitaron con un cliente real como se detalla abajo; quedan interacción
física/inspección visual e interrupciones del proceso o disco físico.
El dueño deja expresamente pendiente multicliente por no disponer de un segundo
cliente. No se sustituye esa aceptación por miembros ficticios en una misión.
No se ha medido rendimiento con carga ni longevidad CE de un servidor real.

Las excepciones de inventario y las decisiones de permisos están documentadas
en [PRODUCT-DECISIONS.md](PRODUCT-DECISIONS.md). Se conserva groups.json v1.
El dueño autoriza fusionar tras el gauntlet. La fusión conserva estos límites
de aceptación: #10 sigue abierta para las verificaciones pendientes y #14
mantiene aplazada la migración v2. La decisión de #15 está resuelta.

## Continuación de issues y PR #17

Código 3c3d85b: al disolver un grupo conserva la reserva de un nombre legacy
si otro grupo sigue usándolo. Opus 5.5 r1 MERGE_OK estático; regresión nativa
antes/después completada, descrita abajo. Validador: 0 errores/17 avisos iguales a la base;
PBO 41 scripts/62 recursos exactos, SHA-256
`2ab9492757d1bff8878b76caaef9348b8be9316d5b7647dca36669a9c0b141de`.

Sobre a8d4a01, run `02b4654c-320c-466a-85c4-9b1dc1ac4763`:
cliente real, **28 comprobaciones UI/0 fallos** (callbacks reales de apertura,
Escape/cierre, confirmación/timeout de salida, caché conservada hasta respuesta,
diálogo de nombre/validación/ACK mediante RPC). No son clics humanos ni
inspección visual. Acciones/inventario: **38 checks/3 fallos** de fixture que entonces
requerían discriminación; no equivalen a 35 casos de producto aprobados porque hay
preparaciones y movimientos asíncronos que necesitan confirmación independiente.
Se conserva el log completo, se cerró el run ordenadamente y se restauraron
PBO y nueve archivos originales por hash. No hay despliegue productivo.

La resolución individual y propuestas de continuación están en
[ISSUES-STATUS.md](ISSUES-STATUS.md).

### Decisiones F14/F16/F31

Código e0111d3: Destroy continuo de 5 segundos y recuperación del nombre temporal
desde el panel, por PlayerBase. F31 conserva sucesión al salir/expulsar por
decisión explícita. Opus 5.5 r1: **MERGE_OK estático**, sin hallazgos bloqueantes.
Validador: 0 errores/17 avisos; UI reconcile: 0 FAIL/0 WARN. PBO extraído con
41 scripts y 62 recursos exactos, SHA-256
`2d819410c99c638b36d06c1d9ceba0a36a67916e03a6ed5ecc2a0d46ad8a0a9c`.

La aceptación nativa final se completó en el run descrito a continuación.
La misión usa el gestor cliente original y su API de input; no simula una
pulsación física de teclado ni sustituye la lógica de las acciones. Los
observadores llaman a super y solo registran movimiento y callbacks.

### F22: regresión nativa completada

Misma fixture y SHA-256 `2488234911310ac65e7d1f2bb6b5bd5e503265ffe26e7e218aaa3c5d78fa2f9c`:

- Base a8d4a01, run `2005712b-f93d-4775-babb-b9c9319e9af6`: 26 checks,
  tres fallos exactos en la reserva del nombre superviviente.
- Candidato e0111d3, run `7beae077-dfa0-4246-adc7-d6c26547ad86`: 26 checks,
  cero fallos. Son 22 aserciones de producto y cuatro escrituras de preparación.
- Tres archivos JSON completos coinciden byte a byte antes/después y con los
  registros esperados, incluidos ID, nombre original, líder, miembro, tier,
  contadores y posición. El formato sigue siendo v1.
- Ambos runs terminaron mediante cierre ordenado; tras cada uno se restauraron
  por hash el PBO original y los nueve archivos del perfil. Después se preparó
  un entorno aislado para la prueba pendiente de acciones/UI.

Existe en ambos runs la misma traza de inicialización de diagnóstico
`PluginConfigDebugProfile`/`mcp_diagplugins.c`, previa a la misión. Se conserva
en la evidencia y no se presenta como ausencia total de errores del entorno.
No hubo error de compilación ni traza nueva en el candidato. Las 26 aserciones
de la misión sí se ejecutaron; la evidencia independiente está en
`acceptance/names-verification.json` del expediente local.

### Acciones, inventario y UI: aceptación final

Run `de49e59c-7d94-4205-9610-8bd183e0f32b`, código e0111d3, DayZDiag 1.29,
cliente y servidor reales, PBO sin file patching: **57 comprobaciones de
acciones y 44 de UI, cero aserciones fallidas**. El conteo incluye preparación;
no equivale a 101 escenarios independientes. F22 también repite 26/0.

- Territorio: un no miembro puede subir/bajar; el servidor rechaza ejecución
  remota. Inventario: blacklist, excepción de colocación, drop sin marca y
  territorio bajado, observando callbacks reales y asentamiento del movimiento.
- Cupo: zona previamente sin cajas; siete objetos reales y recuento de siete
  para límite ocho. El octavo queda en suelo y cuenta una vez; el sobrante
  vuelve al jugador y el contador permanece en ocho.
- Destroy: sin herramienta o fuera de distancia se rechaza; iniciar no
  destruye. Cancelar mediante EndActionInput conserva grupo y bandera.
  Secuencia servidor START/END/START/FINISH/END; una sola finalización,
  **5447 ms**, componente completo y grupo disuelto.
- Upgrade: primera llamada llega a T2; segunda llamada en el mismo tick con
  la referencia anterior se rechaza y conserva la entidad registrada. Esto
  verifica el guard; no es una prueba de concurrencia con dos clientes.
- UI: apertura/cierre/Escape, timeout y confirmación de salida, caché hasta
  ACK, cancelación del diálogo inicial y reapertura desde el panel sin bandera.
  Se rechazan grupo obsoleto y payload truncado. Con el jugador a más de
  1,5 km llegan error y éxito por PlayerBase; nombre definitivo sincronizado,
  diálogo cerrado y botón oculto. El JSON v1 guarda AcceptanceRenamed con
  un miembro líder válido. La etiqueta del test sobre doble clic solo prueba
  envío de nombre válido; no mide cuántos RPC se enviaron.

La primera prueba sobre main tenía tres resultados inconclusos por preparación
y espera insuficiente de movimientos. Los intentos ux-r1/r2 tuvieron errores
de compilación solo del addon de prueba; r3 usó cancelación local; r4 conservaba
el hacha del personaje anterior; r5 saltó el cupo por preparación inválida.
Se conservan sus logs, sin convertirlos en PASS. El run final comprueba sitio
limpio, manos vacías y recuento real antes de afirmar resultados.

Opus 5.5 revisó la ruta de input de la fixture: READY_TO_RUN. En r2 de F16,
limitado al hallazgo nativo nuevo, emitió **MERGE_OK_WITH_LIMIT**: inyectar un
RPC 609 truncado registra una excepción de lectura de string en el motor.
El grupo no cambia y las peticiones posteriores válidas se procesan y guardan.
No hubo caída del proceso ni corrupción observada del JSON. No se verificó
el diagnóstico en un ejecutable no-DIAG. El throttle precede a la lectura.

También se conservan la traza de inicialización PluginConfigDebugProfile y
los avisos de callbacks MCP al cerrar, iguales a los del primer run sobre main.
No se presenta el log como libre de errores del entorno ni se modifica infra.

Cierre ordenado de ambos procesos; addon de prueba archivado, PBO original y
nueve archivos originales del perfil restaurados por SHA-256. Lease liberado
y banco libre al terminar. No es despliegue en el servidor de producción.
Evidencia local: acceptance/actions-ux-r6 (logs, fixture, JSON, launch/close,
restoration y verification.json), dentro del expediente production-20261002.
Resumen portable: [validation/2026-10-02-acceptance.json](validation/2026-10-02-acceptance.json).
