# Reglas de producto confirmadas — 2026-10-02

## Subir y bajar banderas (#15)

Cualquier jugador puede subir y bajar una bandera, aunque no pertenezca a su
grupo. Se mantienen las condiciones de distancia, estado, habilitación del
tier y protección de una T3 alimentada. Esta es la opción C elegida por el dueño;
no se introduce un requisito de pertenencia.

## Exclusiones de muebles (#10, F17)

Los tipos excluidos del conteo de muebles también pueden colocarse sin grupo,
fuera de la zona propia y dentro de territorio ajeno, sin consumir el cupo de
muebles. Se conservan las colisiones y condiciones vanilla. La blacklist
`m_NoDropInForeignTerritoryTypes` tiene prioridad sobre esta exención.

Las banderas y los garden plots mantienen sus reglas específicas. Un kit que
genera una estructura contable no queda exento por el mero hecho de que el kit
no cuente como mueble. Las exclusiones configuradas y las listas A/B se envían
también a jugadores sin grupo.

Los explosivos desplegables vanilla `Plastic_Explosive`, `ImprovisedExplosive`
y `ClaymoreMine` están excluidos por defecto: pueden desplegarse sin grupo y
en territorio ajeno, sin consumir cupo. Se mantienen las condiciones vanilla.
Los archivos de configuración existentes conservan sus listas explícitas.
Para adoptar esta regla, el administrador debe añadir esas tres cadenas al
array `m_FurnitureExcludedTypes` de `config.json`, conservando las entradas
anteriores, y reiniciar el servidor. No hace falta cambiar `m_ConfigVersion`.
La blacklist sigue teniendo prioridad: si se personalizó, retirar de
`m_NoDropInForeignTerritoryTypes` esas clases o una base que las abarque.

## Cobertura de la blacklist

El dueño acepta como excepción los movimientos para los que el servidor no
puede identificar al jugador, incluidos los que van desde cajas o vehículos
al suelo. No se bloquea indiscriminadamente la extracción en territorio propio.
Los movimientos de suelo a suelo tampoco proporcionan un actor en el callback
de ubicación disponible y quedan fuera de ese filtro.

Las acciones de soltar, desplegar y hacer swap tienen comprobaciones de servidor.
El arrastre desde el inventario del jugador se corrige mediante un retorno
diferido. Este último no es un veto atómico: existe una ventana de 250 ms y el
retorno puede fallar si el destino deja de estar disponible. No se garantiza
impedir todos los movimientos posibles de inventario mediante esa recuperación.

El override de `DayZPlayerInventory` que pretendía vetar todos esos movimientos
no es compatible con el motor 1.29: el arranque real rechaza modificar esa clase
nativa. Se usan únicamente los callbacks que el motor admite.

## Persistencia (#14)

El 2026-10-03 el dueño levantó el aplazamiento y pidió implementar v2 ahora.
El lector conserva v1; las escrituras nuevas usan el envelope v2 con contador,
checksum y copia previa verificada. El retorno a v1 exporta el estado actual,
incluidos cambios posteriores a la migración. Ver GROUPS-FORMAT.md.

## Mapas y capacidad (2026-10-03)

Soporte multimapa, con perfiles/CE independientes por mundo. Objetivo 100–120
jugadores. No se interpreta como compartir territorios entre mundos ni como
capacidad ya medida con 120 clientes. El segundo cliente sigue aplazado por
decisión del dueño; las pruebas sintéticas se identifican como tales.

## Refresco de lifetime en configuraciones existentes

`m_MinRefreshLifetime` controla el mínimo de lifetime máximo para refrescar
objetos alrededor de una bandera izada. Por defecto vale `86400` segundos;
un valor negativo, por ejemplo `-1`, desactiva ese refresco. Se puede añadir
o editar esta clave en config.json sin cambiar manualmente `m_ConfigVersion`.
Los valores explícitos se respetan también en archivos v4 o anteriores.
El arranque aplica defaults en memoria y conserva el archivo del administrador.

## Destrucción de bandera (#10, F14)

El dueño exige mantener la acción durante 5 segundos. Iniciar o cancelar no
destruye. Al completar se vuelven a comprobar herramienta válida en manos,
liderazgo, grupo y distancia. Destruir la bandera registrada disuelve el grupo.

Las banderas T1, T2 y T3 no reciben daño, incluidas las creadas por mejora y
las restauradas del almacenamiento. La protección no impide la eliminación
por la acción del líder ni cambia las reglas de expiración del CE. El daño
de terceros no es una vía alternativa para disolver el grupo.

## Recuperar el nombre inicial (#10, F16)

Si el líder cancela el nombre inicial, puede reabrirlo con «Nombrar grupo» en
el panel mientras el nombre siga siendo temporal. No necesita estar junto a
la bandera. Un nombre definitivo no se puede cambiar desde esta opción.

## Sucesión (#10, F31)

El dueño conserva la sucesión al salir o ser expulsado. No habrá sucesión
automática por inactividad; F31 queda resuelto como decisión de producto.
