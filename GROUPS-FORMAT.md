# groups.json v2

Decisión del dueño, 2026-10-03: implementar v2 ahora. Se conserva el DTO lógico
v1, el orden de miembros y todos sus campos (incluido JoinTimestamp legacy).
El lector acepta v1 sin reescribirlo al cargar ni al apagar sin cambios.

## Archivo

```json
{
  "m_Version": 2,
  "m_ExpectedGroups": 0,
  "m_Digest": "adler32:VALOR_B:VALOR_A",
  "m_PayloadBytes": 29,
  "m_PayloadParts": ["{\"m_Version\":1,\"m_Groups\":[]}"]
}
```

El digest del ejemplo es un marcador, no un vector válido. El contenido
protegido son exactamente los bytes UTF-8 de concatenar `m_PayloadParts` en
orden tras decodificar los escapes del JSON exterior. El escritor usa el JSON compacto de DayZ.
Cada parte contiene 1–128 caracteres Unicode y como máximo 512 bytes UTF-8;
`m_PayloadBytes` declara la longitud total. El lector nativo recorta strings JSON
largos a1023bytes: dividir sin cortar caracteres evita esa pérdida silenciosa.
No se ordenan arrays ni se vuelve a serializar el payload para verificarlo.
Así no se depende de que Python y DayZ impriman floats de la misma forma.
Reordenar claves/espacios exteriores no altera el checksum; editar el string
interior sí exige regenerarlo. La sucesión conserva el orden del array.

Checksum [Adler-32, RFC1950](https://www.rfc-editor.org/rfc/rfc1950): a=1,b=0;
por cada byte sin signo, a=(a+byte)%65521; b=(b+a)%65521. Representación exacta
`adler32:<b decimal>:<a decimal>`, sin ceros de relleno. Python usa zlib.adler32
como implementación independiente. Sirve para daños accidentales; no es firma
criptográfica, no autentica al administrador y admite colisiones.

El contador debe coincidir con los grupos del payload. A continuación se
validan las identidades, unicidad de miembros y pertenencia del líder antes
de instalar ningún grupo. Versión lógica interior debe ser 1.
El objeto lógico debe incluir la clave superior `m_Groups`, tanto en v1 como
en el payload v2. Una clave dentro de otro objeto o de un string no cuenta;
se reconocen nombres de clave escapados. Un fallo al inspeccionar las claves
invalida el archivo. `"m_Groups":[]` explícito sigue siendo válido.

## Migración y recuperación

La primera escritura v2 conserva el archivo v1 en `groups.json.pre-v2` y verifica
la copia byte a byte. Tras un rollback y nueva migración usa `.pre-v2.1`, etc.
No sobrescribe esas copias; un reintento reutiliza solo la copia idéntica.
La rotación normal usa `.tmp` y `.bak`, releyendo antes y después de copiar.
Si el final anterior está verificado, un fallo al apartar un backup inválido,
borrar el backup anterior, copiarlo o verificarlo se registra como error y
se continúa promoviendo el tmp verificado. No se siguen ejecutando pasos de
rotación sobre un backup que no se pudo apartar o borrar. Puede faltar una
copia anterior válida; el tmp se conserva hasta verificar el nuevo final.
Tras una rotación fallida, el backup puede ser mucho más viejo que el final:
conserva el estado del último guardado cuya rotación sí terminó. El arranque
solo lo carga si no hay final ni tmp, y no avisa de su antigüedad.
DeleteFile+CopyFile **no es atómico** y el mod no puede forzar la escritura a
disco: un corte de energía poco después de un guardado puede dejar ilegibles
a la vez el final y el backup. Conserva copias externas periódicas del perfil.

- Una versión futura en final/tmp/bak impide cargar/mutar los datos.
- Un final existente inválido implica solo lectura y conserva los candidatos.
  No se sustituye silenciosamente por un backup más viejo.
- Un final válido gana. Un tmp sobrante y un backup inválido se apartan con
  sufijo numerado y copia verificada; no se descartan sus bytes. Un fichero
  que no se puede leer como texto (bytes NUL tras un corte de energía) se
  aparta igual, verificando la copia por su número de bytes.
  Esto incluye un tmp válido más reciente tras una interrupción: pasa a
  `.tmp.discarded` y esa última mutación no se carga automáticamente.
- Sin final, se recupera un tmp válido. Un tmp inválido se aparta a
  `.tmp.discarded` y se intenta el backup; si no se puede apartar, solo lectura.
  Sin tmp activo, se carga el backup válido. Un backup inválido impide cargar.
  Cero grupos con integridad correcta es válido.
- Si no queda final/tmp/bak, se aplica la protección de perfil ausente:
  banderas restauradas con propietario implican solo lectura; sin ellas puede
  iniciarse un mundo nuevo tras la auditoría de arranque. Los archivos apartados
  no se consideran perfiles activos.
- Un guardado de esta sesión que ya verificó su tmp puede terminar su promoción
  pendiente y después guardar el estado nuevo. Un reinicio con final inválido
  requiere recuperación administrativa.

La tolerancia a fallos del backup durante el guardado no cambia las reglas de
arranque. Un backup inválido que siga sin poder apartarse puede bloquear la
carga incluso con un final válido; requiere intervención administrativa.

Estas reglas endurecen intencionadamente la recuperación de un final corrupto
respecto a v1. No prometen que el checksum permita elegir el archivo más reciente:
no hay secuencia ni orden de commit en el formato.

## Volver a un binario v1

1. Detener el servidor y copiar **todo** el perfil SimpleGroup y el CE del mundo
   a un backup fuera de las rutas activas. No ejecutar contra un perfil en uso.
2. Ejecutar `python tools/groups_rollback.py RUTA/SimpleGroup --output RUTA_NUEVA/groups.json`.
   La salida debe no existir. El comando no modifica ningún archivo de entrada.
   Rechaza candidatos inválidos/futuros y final/tmp válidos pero diferentes;
   ese caso requiere resolver y conservar la escritura pendiente primero.
3. Revisar la exportación. Conserva los grupos, miembros, nombres y demás datos
   **actuales**, incluidas las mutaciones posteriores a la migración.
4. Guardar fuera de las rutas activas el final, tmp y bak anteriores. Instalar
   la exportación como final; no dejar un tmp/bak v2 junto al binario antiguo.
5. Arrancar el binario v1 y verificar carga/identidades antes de admitir usuarios.

Restaurar solamente `.pre-v2` pierde cambios posteriores. El exportador no hace
ese rollback destructivo ni promueve archivos automáticamente.

## Multimapa y capacidad

Un perfil SimpleGroup y un almacenamiento CE **separados por mundo/instancia**.
El formato no fija mapa ni coordenadas; no transfiere territorios entre mapas
ni comparte grupos entre servidores. El objetivo del dueño es 100–120 jugadores.
Fixtures de 120 identidades miden el coste de datos; no sustituyen 120 conexiones
ni el rendimiento del conjunto de mods del servidor destino.
