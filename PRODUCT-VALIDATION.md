# Validación del candidato — 2026-10-02

Código probado: `11ba729bd03c6f5eb26d2ef1e58b9f8d52672290`.
Estado: candidato revisado; aceptación de producción pendiente.

## Revisión de producto

Gauntlet con Claude Opus 5.5 (`claude-opus-5-5`), contextos independientes.
Desde r2 se revisaron exclusivamente cambios y cierre de hallazgos.
Acciones: r2 OK. Territorio, persistencia, UI/sync y F17: r3 OK.
El guard de colisión de ID se concilió por Codex según la autorización desde r3.

## Comprobaciones ejecutadas

- Validador: 0 errores, 17 avisos iguales a la base, sin avisos nuevos.
- UI: 3 layouts, 41 fuentes y 49 claves; 0 fallos y 0 avisos de reconciliación.
- PBO extraído: 41 scripts y 62 recursos coinciden con las fuentes; config.bin
  y cuatro modelos ODOL presentes. SHA-256:
  `a52c02fe217ba83d85f84810ac0f296fd3f2a0dbdbf44ce11fc15b3b3873153a`.
- DayZ 1.29.163709, sin file patching: servidor y cliente compilan y cargan el mod.
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

## Límites de la evidencia

El parser cliente se ejercitó con serializadores nativos en el proceso servidor;
eso no prueba el transporte RPC a varios clientes. La reconciliación de layouts
no prueba interacción visual. Quedan por ejercitar con jugadores las acciones,
inventario y UI, además de interrupciones del proceso o disco físico.
El dueño deja expresamente pendiente multicliente por no disponer de un segundo
cliente. No se sustituye esa aceptación por miembros ficticios en una misión.
No se ha medido rendimiento con carga ni longevidad CE de un servidor real.

Las excepciones de inventario y las decisiones de permisos están documentadas
en [PRODUCT-DECISIONS.md](PRODUCT-DECISIONS.md). Se conserva groups.json v1.
La PR permanece en borrador hasta completar la aceptación del producto.
