# Textify

A small tool which allows to copy text from dialogs and controls which don’t allow it otherwise.

[🏠 Homepage](https://ramensoftware.com/textify)

![Screenshot](screenshot.png)

## Selection capture and vault clippings

The keyboard and mouse shortcuts copy the current selection into the Textify window. The previous clipboard content is restored afterwards. Clipboard-history enumeration is deliberately avoided so opening the window stays responsive.

Open **More Settings** to edit the labeled Code DB and Manual DB destinations in a compact list. The order in each list is the drop-down order:

```ini
[clippings]
code_db=[
	"Python":"D:\path\to\vault\python",
	"Frida":"D:\path\to\vault\frida"
]
manual_db=[
	"Notes":"D:\path\to\vault\manual"
]
```

Both arrays must be present, but either may be empty. Entries are comma-separated (a final comma is optional), every entry requires a quoted label and quoted path, and labels within one list are unique without regard to case. Backslashes are literal, so a root path is written naturally as `"python":"D:\"`.

More Settings converts this friendly view to reliable numbered INI sections when you save, and converts those sections back to the friendly view whenever you open it. Unrelated sections, keys, and comments outside the managed destination definitions are preserved. A friendly array already present directly in `Textify.ini` is also recognized and will be normalized on the next More Settings save. The old `code_directory`, `code_db`, or `directory` setting remains a single Code DB fallback, and `manual_directory` or `manual_db` remains a single Manual DB fallback when no destination list is present.

Each DB button has a drop-down list. The main button saves to the current default. Choosing a labeled drop-down item saves to that destination immediately and makes it the default for the rest of the running session. A restart selects the lowest-index configured item again. Ctrl+L follows the current Code DB default; Alt+L follows the current Manual DB default.

Relative paths are resolved from the Textify executable directory. Saving writes the edited window text verbatim. A line beginning with `# /` supplies the filename from the text after that marker (or from the following line when the marker is alone). Windows-invalid filename characters are removed. A collision appends an increasing number; content without a usable marker falls back to `clips1.md`, `clips2.md`, and so on. Existing files are never overwritten.
The application always reuses this `Textify.ini` file even when a newly built executable temporarily has a different filename.

`Textify.ini` is monitored while Textify runs. Externally saved changes are debounced and applied automatically in about one second, including destination lists, capture hotkeys, web buttons, tray options, and other settings. An already open clipping window refreshes its destination list when a drop-down is opened and reloads it again before saving.

- Ctrl+right-click capture adds `//?` before a recognized leading comment header and `# /` before the first code line. Without a comment header it adds only `# /`.
- Ctrl+L performs **Add to Code DB** using its current default.
- Alt+L performs **Add to Manual DB** using its current default.
- The first four web buttons use Alt+1, Alt+2, Alt+3, and Alt+4.
