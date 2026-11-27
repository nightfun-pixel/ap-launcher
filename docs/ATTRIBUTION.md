[English](ATTRIBUTION.md) · [Русский](ATTRIBUTION.ru.md)

# Attribution

BLUEWARD ships one third-party code component and one visual asset. Official release builds also embed a background clip that is not distributed in the source tree.

## Code

| Component | Version | Location | License |
| --- | --- | --- | --- |
| miniz | 2.2.0 | `third_party/miniz/` | MIT |

The full MIT text is in `third_party/miniz/LICENSE`. miniz is used only for raw DEFLATE decompression inside the patch ZIP reader (`src/core/zip.cpp`); ZIP parsing, CRC-32, name validation and extraction are BLUEWARD code.

No other third-party source is vendored. The launcher uses only Windows system libraries: D3D11, DXGI, Direct2D, DirectWrite, DWM, WIC, Media Foundation, WinHTTP, BCrypt, Cabinet-free ZIP (miniz), Shell, COM.

## Visual assets

| Asset | Origin | Terms |
| --- | --- | --- |
| `res/blueward.ico` | Original BLUEWARD mark produced for this project | Project asset; not derived from any game or donor icon |
| Background clip (release build only; not in this repository) | Official Azur Promilia Japanese site hero video, trimmed and re-encoded to 1280×720, 30 fps, H.264 High, no audio | © Manjuu Co., Ltd. All rights reserved. © Yostar, Inc. All rights reserved. See below |

The Japanese secondary-work guideline allows non-commercial fan works but excludes works that directly copy official materials. The background clip is official promotional media, not a fan asset. It is embedded only in the operator's release build; the source tree does not contain it. Do not redistribute the clip or a build containing it without permission from Yostar/Manjuu. Attribution alone does not grant a license.

The UI uses the system Segoe UI family; no font file is shipped. No donor icon, donor clip, donor font, donor screenshot, game logo or wizard art is present.

## Names and trademarks

Azur Promilia and related marks belong to their owners. BLUEWARD is an unofficial fan tool and is not affiliated with, endorsed by, or sponsored by Yostar, Manjuu or Gryphline.
