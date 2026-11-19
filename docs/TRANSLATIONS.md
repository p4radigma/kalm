# Translations

[← Back to README](../README.md)

---

## Built-in Languages
[`translations/en.json`](../translations/en.json) (**English**, default) and [`translations/ru.json`](../translations/ru.json) (**Russian**) are embedded directly into the Kalm binary via Qt Resources (`resources.qrc`) and can also be overridden on disk.

You can switch the active language live in **Drawer (☰) → Language** (`System locale`, `English`, `Русский`, etc.) or via the CLI:
```bash
kalm lang en
kalm lang ru
kalm lang auto
```

---

## Adding a New Language (Zero Rebuild Required)

You can create and test a new translation (for example, German `de`) **without recompiling** Kalm:

1. Copy [`translations/en.json`](../translations/en.json) to one of the runtime translation directories:
   - **Linux:** `~/.local/share/kalm/translations/de.json`
   - **Windows:** `translations/de.json` next to `kalm.exe` (or `%LOCALAPPDATA%\kalm\translations\de.json`)
2. Set `"lang_name"` at the top of the JSON file to the native name of the language (e.g. `"Deutsch"`).
3. Translate the string values:
   - Keep `%1`, `%2` placeholders intact where present.
   - Any omitted or untranslated keys automatically fall back to `en.json`.
4. Restart Kalm (or re-select the language in **Drawer (☰) → Language** / `kalm lang de`).

---

## Contributing a Built-in Translation

To bundle a new language directly into official releases:
1. Add `translations/<code>.json` to the repository.
2. Register `<file>translations/<code>.json</file>` in [`resources.qrc`](../resources.qrc).
3. Run `ctest --test-dir build --output-on-failure` and open a Pull Request!
