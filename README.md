# Break Reminder (Geode mod)

Popup que te avisa que hagas un descanso después de X minutos jugando (1 a 180 min, configurable).
Hecho para GD 2.2081 y Geode v5.11.2.

## Cómo compilarlo (sin instalar nada)
1. Creá un repo nuevo en GitHub y subí todo el contenido de esta carpeta (incluida la carpeta `.github`).
2. Entrá a la pestaña **Actions** del repo: el workflow "Build Geode Mod" corre solo con cada push.
3. Cuando termine (tilde verde), abrí la corrida, bajá el artifact **Build Output** y descomprimilo.
4. Copiá el archivo `.geode` a `%LOCALAPPDATA%\GeometryDash\geode\mods\` y abrí el juego.
5. Los ajustes están en Geode > Mods > Break Reminder > Settings.

## Cambiar el nombre
En `mod.json` cambiá `"id"` (formato `tuusuario.break-reminder`) y `"developer"`.
