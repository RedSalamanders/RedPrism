# RedPrism

Windows-native raster composition (Direct3D 11 + DxUi). This guide is not a behavior authority; see `Specs/`.

## Run

```powershell
.\build.ps1
.\.build\x64\Debug\RedPrism.exe
.\.build\x64\Debug\RedPrism.exe --warp
.\.build\x64\Debug\RedPrism.exe --help
```

`--screenshot <png> [--dpi 96|144] [--warp]` captures the titled window without activating it.

## Shell

One window: menu, document tab, tool rail, canvas, layers, status. New / Open / Save use the common file dialog.
Drop a `.prism` package, an exploded project folder, or an image onto the window. Photoshop-style tool keys
(V/M/L/W/C/B/E/J/S/R/G/U/I/H/Z, A idle) apply when the canvas has focus; Escape returns focus to chrome.
Space pans. Wheel zooms.

Empty-document harness captures:

- [96 DPI](screenshots/empty-shell-96.png)
- [144 DPI](screenshots/empty-shell-144.png)

Transform, selection, brush, and filters are later phases (indexed plans under `Specs/Plans/WIP/`).
