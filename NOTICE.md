# Source and license notices

Open Wind is distributed under GPL-3.0-only; see [COPYING](COPYING).
Existing source notices and attribution remain applicable.

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
