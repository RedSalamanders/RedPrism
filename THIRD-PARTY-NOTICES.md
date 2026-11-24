# Third-party notices

RedPrism is copyright (c) 2026 RedSalamander, under the MIT license in [LICENSE.txt](LICENSE.txt). The components below
are compiled into or shipped with the portable package and keep their own terms. Library versions are the minimums in
[vcpkg.json](vcpkg.json), restored at its pinned builtin-baseline, and the package carries each library's vcpkg
copyright file under `ThirdPartyNotices\`. A new dependency or asset extends this list, with its full notice, before
it is distributed.

- **Inherited code.** RedPrism is a Windows port of an earlier application, copyright (c) 2026 Wonder Assembly LLC,
  MIT. Its full [notice](ThirdParty/Notices/InheritedCode.txt) ships as `ThirdPartyNotices\inherited-code.txt`.
- **DxUi**, at the commit in [Dependencies/DxUi.lock.json](Dependencies/DxUi.lock.json), copyright (c) 2026
  RedSalamander, MIT; its license ships as `ThirdPartyNotices\dxui.txt`. That revision compiles in WIL (below); toml++
  3.4.0, Mark Gillard, MIT; a math symbol table derived from KaTeX 0.16.11, Khan Academy and contributors, MIT;
  rapidyaml 0.16.0, Joao Paulo Magalhaes, MIT, with its bundled debugbreak (Scott Tsai, BSD 2-Clause) and fast_float
  (MIT) notices; tables generated from Unicode 17.0.0, copyright Unicode, Inc., Unicode License V3; and an HTML
  named-character table from the WHATWG snapshot of 18 October 2026, copyright WHATWG (Apple, Google, Mozilla,
  Microsoft), BSD 3-Clause for code and CC BY 4.0. Source builds ship the full notices from that revision in
  `ThirdPartyNotices\\dxui-*.txt`. SDK builds ship the verified SDK notice bundle under `ThirdPartyNotices\\DxUiSdk`.
- The **RedMonitor** client header, at the commit in [Dependencies/RedMonitor.lock.json](Dependencies/RedMonitor.lock.json),
  copyright (c) 2026 RedSalamander, MIT. Its [notice](ThirdParty/RedMonitor/LICENSE.txt) ships as
  `ThirdPartyNotices\redmonitor.txt`.
- **Lucide** icons: the tool rail draws 16 icons from the commit in [Dependencies/Lucide.lock.json](Dependencies/Lucide.lock.json),
  Lucide Icons and Contributors, ISC, with the MIT notice of Cole Bemis for the icons derived from Feather. The full
  [license](ThirdParty/Lucide/LICENSE) ships as `ThirdPartyNotices\lucide.txt`.
- **Clipper2** 2.0.1 or later, Angus Johnson, Boost Software License 1.0, through vcpkg, and a maintained source fork of
  Clipper2 2.0.1 compiled from `Common/BoundedClipper/`, whose full [license](Common/BoundedClipper/LICENSE) is kept
  beside it.
- **WIL** (Windows Implementation Libraries) 1.0.260126.7 or later, Microsoft Corporation, MIT, header-only.
- **yyjson** 0.13.0 or later, YaoYuan, MIT, shipped as `yyjson.dll`.
- **libzip**, built without optional features and shipped as `zip.dll`, Dieter Baron and Thomas Klausner, BSD
  3-Clause, with **zlib** (`z.dll`), Jean-loup Gailly and Mark Adler, zlib license.
- The **Microsoft Visual C++ runtime** ships beside the executable under the Microsoft Visual Studio
  distributable-code terms. Windows SDK and system libraries are used under Microsoft's terms; Segoe Fluent Icons is a
  Windows font and is not shipped. The CC0 RAW images the tests use are not shipped.
