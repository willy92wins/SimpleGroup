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
- C4, IED y claymore se pueden colocar en cualquier sitio, sin contar como muebles del territorio.
- El servidor rechaza colocar el kit de bandera a más de 8 m del jugador.
- Logs: el apagado ya no registra un error por banderas borradas; los mensajes del mod pasan por su propio logger.

### Empaquetado

- Nombre público SimpleGroup; autor Return; firmado con la clave `Return0`.
- Los modelos usan los materiales vanilla por su ruta del juego en vez de copias.
- Nuevos `INSTALL.md` (servidor, claves, Dabs) y `NOTICE.md`.
