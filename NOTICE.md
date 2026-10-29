# Source and license notices

## Default license

Unless a file carries a separate license notice, Open Wind is distributed under
GPL-3.0-or-later; see [COPYING](COPYING), with the Modding Exception and GPL-3.0
Linking Exception (with Corresponding Source) in [EXCEPTIONS.md](EXCEPTIONS.md).
The Modded Code is Skyrim and its variants. The Modding Libraries are SKSE and
CommonLib and its variants, matching the applicable upstream exceptions.

## Public API headers

All `.h` files under `include/OpenWind/` are licensed under LGPL-3.0-or-later;
see [COPYING.LESSER](COPYING.LESSER) together with [COPYING](COPYING).
This covers the sampling, renderer, UI, authoring, bridge and supporting public
data headers, including their inline helpers. Their SPDX notices identify this
scope explicitly. The implementation under `src/` retains the default license.

Consumers using the LGPL headers must comply with their terms. This separate
license does not relicense the Open Wind implementation or third-party code.

## Attribution and dependencies

Existing source notices and attribution remain applicable. Third-party files
retain their respective licenses.

- Wind runtime, effects, and renderer contracts were extracted from
  [Open Shaders](https://github.com/alandtse/open-shaders), which derives
  from [Community Shaders](https://github.com/community-shaders/skyrim-community-shaders).
- The HDT bridge includes work derived from Dynamic Wind Field - HDT Physics.
  Its FSMP host headers and GPL notice are under `extern/FSMP/`.
- The Dynamic Wind bridge includes the companion Open-Wind-Field integration.
  Its generic host API provenance is documented under `extern/DynamicWind/`.
- CommonLibVR and its OpenVR dependency are fetched at pinned revisions by
  CMake. Their original licenses remain with those dependency sources.
- vcpkg provides DirectXTK, nlohmann/json, rapidcsv, spdlog, and their
  dependencies under their respective upstream licenses.

This repository contains no Open Shaders shader assets, game assets, build
outputs, or host-mod implementation sources. Host API headers are included
only where needed by the optional bridge DLLs.
