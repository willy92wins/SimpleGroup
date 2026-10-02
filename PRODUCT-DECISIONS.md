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

Se conserva groups.json v1. El formato v2 sigue aplazado según la decisión
registrada en #14; estas correcciones no migran datos.

## Refresco de lifetime en configuraciones existentes

`m_MinRefreshLifetime` controla el mínimo de lifetime máximo para refrescar
objetos alrededor de una bandera izada. Por defecto vale `86400` segundos;
un valor negativo, por ejemplo `-1`, desactiva ese refresco. Se puede añadir
o editar esta clave en config.json sin cambiar manualmente `m_ConfigVersion`.
Los valores explícitos se respetan también en archivos v4 o anteriores.
El arranque aplica defaults en memoria y conserva el archivo del administrador.
