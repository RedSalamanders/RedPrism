# Command line and files

Every switch is declared in `RedPrism/CommandLine.h`. `--help` prints that catalog.

| Switch | Meaning |
| --- | --- |
| *(none)* | Titled editor window |
| `--warp` | Microsoft Basic Render Driver |
| `--self-test` | Hidden WARP smoke (used by `test.ps1`) |
| `--screenshot <png>` | Capture without activation, then exit |
| `--dpi 96\|144` | Layout DPI for `--screenshot` |
| `--help` | Print the catalog and exit 0 |

Projects are ZIP `.prism` files or exploded folders (`manifest.json` + `images/`). PNG, JPEG, TIFF, and HEIC (when
WIC provides HEIF) import onto the canvas. Export PNG or JPEG from the File menu.
