# Instalación CE en cada mapa

Copiar la carpeta `SimpleGroup` de aquí dentro de la misión de cada instancia.
Añadir dentro de `<economycore>` de su `cfgeconomycore.xml`:

```xml
<ce folder="SimpleGroup">
    <file name="types.xml" type="types" />
</ce>
```

No duplicar estas clases si ya están en el types.xml de esa misión. Los nombres
son los cuatro tipos spawnables del addon; el holograma y la base abstracta no
son entradas CE. Nominal/min 0 conserva la adquisición mediante crafting/admin;
no añade loot ni coordenadas, categorías de uso o tiers propios de un mapa.

Los valores base siguen TerritoryFlagKit (14400s) y TerritoryFlag (604800s)
del types.xml vanilla disponible. Una bandera de grupo registrado con progreso
mayor que 0 (también parcialmente levantada) recibe 3888000 s por ApplyGroupLifetime.
Las banderas con ID pendientes tras fallo de carga reciben la misma protección
si su progreso es mayor que 0. El refresco deja de aplicarse con progreso 0;
eso no acorta retroactivamente el
lifetime que ya había recibido. La base cercana sigue las reglas de
m_MinRefreshLifetime y m_RecalibrationIntervalSeconds del mod.
La restauración efectiva de estos valores tras reiniciar debe comprobarse con
el CE de la instancia; este archivo por sí solo no acredita esa prueba.

Usar **perfiles y almacenamiento CE distintos por mapa/instancia**. Multimapa
significa que el mod no depende de una geografía concreta; no implica compartir
grupos/territorios entre mundos. No copiar groups.json de un mundo a otro.

Objetivo: 100–120 jugadores. Un test de 120 identidades sin clientes mide datos,
no tráfico, FPS bajo jugadores reales ni interacciones con otros mods. La
aceptación operativa debe registrar hardware, mapa, mods, número de grupos y
objetos, latencia de guardado/recuento y pérdida de ticks. Ver PRODUCT-VALIDATION.

No se modifica automáticamente la misión de producción. Conservar un backup
completo y verificar en el RPT que el CE carga sus registros y restaura objetos
al reiniciar antes de declarar persistencia comprobada en ese servidor.
