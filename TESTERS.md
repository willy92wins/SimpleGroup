# Pruebas de SimpleGroup 1.0.0-rc1

Versión candidata: buscamos fallos antes de publicarla. Marca cada punto y apunta lo que no salga como se describe.
Hacen falta al menos tres jugadores: un líder, un miembro y alguien sin grupo.

Antes de empezar: servidor y cliente en DayZ 1.29 con Dabs Framework y SimpleGroup ([INSTALL.md](INSTALL.md)). La
tecla `P` abre el panel de grupo. Los textos del juego van como «inglés / español» y los valores son los de un
`config.json` nuevo.

## 1. Conexión

- [ ] Entras al servidor sin que te expulse por firma.
- [ ] La tecla `P` abre el panel de grupo.

## 2. Kit y colocación

- [ ] Fabricas el kit con un palo largo (Long Wooden Stick) y un trapo (Rag): sale «Territory Flag Kit».
- [ ] Al colocarlo ves el holograma del mástil T1 y la bandera queda donde la pusiste.
- [ ] A menos de 500 m de otra bandera no se puede colocar: «Too close to another territory / Demasiado cerca de
  otro territorio».

## 3. Registrar el territorio

- [ ] Sin grupo, mantén F sobre la bandera: «Register Territory / Registrar territorio». Se abre el diálogo de
  nombre (letras sin tildes ni ñ, números, espacios, - y _).
- [ ] Si cancelas el nombre, puedes reabrirlo desde el panel con «Name group / Nombrar grupo» mientras sea temporal.
- [ ] En el panel apareces como líder.

## 4. Subir y bajar la bandera

- [ ] Cualquier jugador, sea o no del grupo, puede subirla y bajarla manteniendo F: «Raise Flag / Subir bandera» y
  «Lower Flag / Bajar bandera».
- [ ] El mástil anima al subir y bajar.

## 5. Invitar y unirse

- [ ] Un miembro, con la bandera de su grupo izada, pulsa F sobre ella: «Activate Invite Mode / Activar modo
  invitación». La invitación dura 10 s.
- [ ] Un jugador sin grupo mantiene F sobre la bandera en ese tiempo: «Join Group / Unirse al grupo». Entra en el
  grupo.
- [ ] Pasados los 10 s ya no puede unirse.
- [ ] El grupo admite como máximo 6 miembros.

## 6. Mejoras y aspecto

- [ ] T1 → T2: pon un tronco (Wooden Log) y una cuerda (Rope) en la bandera y mantén F con un mazo (Sledgehammer):
  «Upgrade to Tier 2 / Mejorar a nivel 2».
- [ ] T2 → T3: pon 6 de leña (Firewood), 60 clavos (Nails) y 10 piedras (Stones) y mantén F con un pico (Pickaxe):
  «Upgrade to Tier 3 / Mejorar a nivel 3».
- [ ] La T3 tiene un hueco para batería de coche. Con la batería cargada puesta, la bandera no se puede bajar.
- [ ] Mira de cerca el kit y los tres mástiles, de día y de noche: madera, cuerda, trapo y tablas con su textura
  normal, sin zonas blancas, moradas, negras o brillantes. **Este punto es importante en esta versión:** los
  materiales pasaron a usar los del juego.

## 7. Construir

- [ ] Sin bandera no puedes construir: «You need a territory flag to build / Necesitas una bandera de territorio para
  construir».
- [ ] Solo se construye a 30 m o menos de tu bandera y con ella izada. Fuera: «You must build within your territory
  / Debes construir dentro de tu territorio».
- [ ] Límite de muebles por tier: 8, 12 y 16. Al pasarlo: «Furniture limit reached in your territory / Límite de
  muebles alcanzado en tu territorio».
- [ ] Como máximo 3 huertos por bandera: «Garden plot limit reached for your territory / Límite de huertos alcanzado
  en tu territorio».
- [ ] C4, IED y claymore se pueden colocar en cualquier sitio, también en territorio ajeno.
- [ ] Algunos objetos no se pueden soltar en territorio de otro grupo: «You cannot drop this item in another group's
  territory. / No puedes soltar este objeto en el territorio de otro grupo.»

## 8. Banderas indestructibles

- [ ] Disparar, golpear o hacer explotar algo junto a una bandera T1, T2 o T3 no la daña.

## 9. Salir, expulsar y sucesión

- [ ] Un miembro sale del grupo desde el panel («Leave group / Abandonar grupo») y deja de aparecer en él.
- [ ] El líder expulsa a un miembro desde el panel («Kick / Expulsar»).
- [ ] Si sale el líder, el nuevo líder es el miembro más antiguo del grupo.

## 10. Destruir la bandera

- [ ] Solo el líder, con un hacha en las manos, manteniendo 5 s «Destroy Flag / Destruir bandera». Al terminar, el
  grupo se disuelve.
- [ ] Cancelar antes de los 5 s no destruye nada.

## 11. Reinicio del servidor

- [ ] Tras reiniciar, el grupo, sus miembros, el nombre, el tier y la bandera siguen igual.
- [ ] (Admin) En el perfil del servidor existen `SimpleGroup/config.json` y `SimpleGroup/groups.json`, y tras el
  segundo guardado aparece `groups.json.bak`.

## Cómo informar de un fallo

Para cada fallo: hora aproximada, qué hiciste, qué esperabas, qué pasó y, si es visual, una captura. El administrador
del servidor adjunta de esa sesión el `script_*.log` y el `.RPT` del servidor, y la carpeta `SimpleGroup/` del
perfil (`config.json`, `groups.json` y, si existen, `.bak` y `.tmp`). Si el fallo es del cliente, añade también su
`script_*.log`, que está en `%LOCALAPPDATA%\DayZ`.

## Problemas conocidos (no hace falta informar)

- El holograma del kit no muestra el mismo radio de territorio que aplica el servidor, y no hay mensaje (SG-22).
- Cavar un huerto puede toparse con el límite de muebles (SG-19).
- Con `m_DestroyDeployedOnDissolve = true` (por defecto está en false), un grupo disuelto puede seguir viéndose en
  el cliente hasta reconectar (SG-13).
- Una bandera de grupo bajada puede caducar antes de lo previsto tras un reinicio si el CE restaura el máximo de su
  types.xml (SG-06).
- Cualquier jugador puede quitar la batería de una T3 (decisión de diseño).
- Los nombres de los huecos de la bandera (Wooden Log, Rope...) solo están en inglés.
- No se ha probado junto con LFPowerGrid.
- La 1.30 de DayZ todavía no está soportada.
- Rendimiento: cada cambio de miembros guarda el fichero completo. Con 100 o más jugadores, vigila si hay tirones
  (SG-25).
