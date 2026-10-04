# Cambios

## 1.0.0-rc1 — candidato para testers (2026-10-04)

Primera versión candidata para el Steam Workshop.

### Endurecimiento previo al release

- Guardado de grupos: un `groups.json.bak` bloqueado ya no impide guardar; un `groups.json.tmp` inválido sin fichero
  final se aparta y se intenta el backup; un fichero sin la lista de grupos ya no se acepta como «cero grupos».
- Sucesión: al salir el líder, hereda el miembro más antiguo.
- Configuración: las listas omitidas recuperan sus valores por defecto en cualquier versión de `config.json`; radios
  hasta 10000 m e invitaciones hasta 3600 s.
- Banderas T1/T2/T3 indestructibles.
- C4, IED y claymore se pueden colocar en cualquier sitio, sin contar como muebles del territorio. Es el valor
  por defecto de un `config.json` nuevo; en uno existente hay que añadir las tres clases a
  `m_FurnitureExcludedTypes` (ver PRODUCT-DECISIONS.md).
- El servidor rechaza colocar el kit de bandera a más de 4 m del jugador (el juego lo coloca a 1-2 m).
- Logs: el apagado ya no registra un error por banderas borradas; los mensajes del mod pasan por su propio logger,
  y los objetos bloqueados al soltarlos o colocarlos en territorio ajeno quedan registrados en el log del servidor.
- Guardado: un `groups.json.bak` que no se pudo rotar ya no bloquea los guardados siguientes de la sesión.
- Arranque: un `groups.json.tmp` o `.bak` lleno de bytes NUL tras un corte de energía ya no deja el servidor en solo
  lectura; se aparta conservando sus bytes y se carga el fichero válido.

### Empaquetado

- Nombre público SimpleGroup; autor Return; firmado con la clave `Return0`.
- Los modelos usan los materiales vanilla por su ruta del juego en vez de copias.
- Nuevos `INSTALL.md` (servidor, claves, Dabs) y `NOTICE.md`.
