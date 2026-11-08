# Tessera Localization & Multi-Language System

This directory contains localization files for the **Tessera Game Engine Editor**.

---

## 1. Engine UI Localization (`lang/`)

The files in this directory translate the editor itself (toolbars, menus, viewports, inspector properties, and modal dialogs).

### Format
Files are UTF-8 JSON dictionaries mapping English source keys to translated strings:

```json
{
  "code": "es",
  "name": "Español",
  "strings": {
    "File": "Archivo",
    "Edit": "Editar",
    "Scene": "Escena",
    "View": "Vista",
    "Export Standalone Game...": "Exportar Juego Independiente..."
  }
}
```

### Self-Healing & In-Memory Fallbacks
- All default strings (370+ keys) are compiled directly into the `Tessera.exe` binary.
- If `Tessera.exe` is distributed as a standalone binary without this `lang/` folder, the engine will automatically self-heal and recreate `lang/en.json` and `lang/es.json` on startup.
- If running in a read-only environment (e.g. `Program Files`), the engine gracefully falls back to the embedded in-memory dictionaries.

---

## 2. Game Project Localization (`assets/localization/`)

To translate your **own games** (dialogues, quest logs, menus, HUDs):
1. Open the **Game Localization Editor** in Tessera via **`Project -> Game Localization...`** or press **`Ctrl+L`**.
2. Select an existing language or click **`[+ New Language]`** (e.g. `fr`, `de`, `ja`, `it`).
3. Click **`[+ Add Key]`** to create translation keys (e.g. `menu_start`, `dialog_welcome`) and enter translations in the grid.
4. Translations are saved to `assets/localization/<code.json>` in your project folder, and are automatically packaged when exporting standalone games.

### Using Game Translations:
- **In UI Nodes**: In the Inspector for any `UITextNode` or `UIButtonNode`, set the **`Localization Key (Optional)`** field.
- **In Lua Scripts**: Call `TR("key")` or `Localization.Translate("key")`.
- **In-Game Language Menus**: Call `Localization.SetLanguage("es")` to switch the game language at runtime. All bound UI elements update dynamically on the next frame!
