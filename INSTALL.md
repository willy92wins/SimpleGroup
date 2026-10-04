# Instalación de SimpleGroup

## Requisitos

- DayZ 1.29 en servidor y cliente. La 1.30 queda pendiente de revalidar: hoy Dabs Framework no compila en la 1.30
  Experimental.
- **Dabs Framework** (Steam Workshop `2545327648`) en el cliente **y** en el servidor: SimpleGroup lo usa desde
  scripts que también compilan en el servidor.

## Servidor

1. Copia `@Dabs Framework` y `@SimpleGroup` a la carpeta del servidor (o a la que uses para mods).
2. Copia las claves a la carpeta `keys` del servidor: `@SimpleGroup\keys\Return0.bikey` y
   `@Dabs Framework\keys\dab.bikey`.
3. Carga los dos mods con `-mod`, no con `-serverMod`, porque el cliente también los necesita. Por ejemplo:
   `"-mod=@Dabs Framework;@SimpleGroup"`.
4. Recomendado: `verifySignatures = 2;` en `serverDZ.cfg`.
5. Economía central: copia la carpeta `SimpleGroup` de `server/` (en el paquete, `ServerFiles/`) a la misión de
   cada instancia y regístrala en su `cfgeconomycore.xml` como indica el `README.md` de esa misma carpeta.

## Ficheros del mod

Viven en la carpeta de perfil del servidor (`-profiles=`), dentro de `SimpleGroup/`:

- `config.json`: se crea con los valores por defecto en el primer arranque. Si no se puede leer, el servidor usa los
  valores por defecto durante esa sesión y conserva el fichero tal cual. Al actualizar el mod, las opciones nuevas
  toman su valor por defecto sin reescribir el fichero.
- `groups.json`: grupos y territorios. `groups.json.bak` es la copia anterior y `groups.json.tmp` una escritura en
  curso. El formato y la recuperación están en [GROUPS-FORMAT.md](GROUPS-FORMAT.md).

Guarda copias periódicas de la carpeta `SimpleGroup/` del perfil, y siempre antes de actualizar el mod: un corte
de energía poco después de un guardado puede dejar ilegibles a la vez `groups.json` y su `.bak`.

Límites que el servidor aplica al cargar `config.json`:

| Opción | Por defecto | Límites |
|---|---|---|
| `m_MaxGroupSize` | 6 | 1 a 20 |
| `m_BuildRadiusMeters` | 30 | 5 a 10000 |
| `m_TerritoryRadiusMeters` | 500 | 50 a 10000 |
| `m_InviteDurationSeconds` | 10 | 5 a 3600 |
| `m_GroupNameMinLength` / `m_GroupNameMaxLength` | — | 1 a 48, mínimo ≤ máximo |

Las reglas de producto (banderas, cupos de muebles y huertos, explosivos) están en
[PRODUCT-DECISIONS.md](PRODUCT-DECISIONS.md).

## Cliente

- Suscríbete a Dabs Framework y a SimpleGroup en el Workshop.
- La tecla `P` abre el panel de grupo. Se cambia en Controles, categoría SimpleGroup.

## Problemas frecuentes

- **El servidor te expulsa al entrar por la firma:** falta `Return0.bikey` o `dab.bikey` en `keys`, o la versión del
  mod del cliente no coincide con la del servidor.
- **El servidor no arranca o el log de script muestra errores de SimpleGroup:** comprueba que Dabs Framework está en
  `-mod` del servidor, no solo en el cliente.
