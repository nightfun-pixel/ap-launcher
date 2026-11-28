[English](RELEASE-0.1.5.md) · [Русский](RELEASE-0.1.5.ru.md)

# BLUEWARD 0.1.5

BLUEWARD 0.1.5 supports the seven-payload r6 patch and starts the game directly from the installed
state. Patch r6 fixes full-width Cyrillic metrics without changing the size of the launcher-checked
font bundles.

## What changed

- The manifest accepts five legacy or seven current payloads. The two new payloads are
  `font-a00.r` and `font-b00.r`, the external TMP fallback source bundles.
- Patch `2026.11.28-6` rewrites the `hmtx` metrics of the 67 Cyrillic glyphs in both font bundles:
  `advance = (xMax - xMin) + 2 * round(upm * 0.055)`, `lsb = round(upm * 0.055)`. Blank glyphs are
  untouched. The TTF and the UnityFS bundle keep their original byte size; the bundle CRC32 is
  balanced. The official launcher's size check therefore stays clean.
- `startup.r` is rebuilt with the same byte size and points `ast_b00_origin_SDF` at the MiSans
  `b00` Font already inside the archive. `onest-font.r` stays byte-identical to the original.
- The installed state now shows `Играть`; it starts `AzurPromiliaJP.exe` with
  `-start=azurpromilia_launcher` from the configured folder.
- The MiSans **glyph shapes** are not in this patch yet: the hmtx fix keeps the existing OT-Shin
  Maru outlines but makes them proportional. A size-preserving MiSans subset is the next step.

## Artifact

| File | Size | SHA-256 |
| --- | ---: | --- |
| `BLUEWARD.exe` | 5,097,472 | `faffb25704b183c4285cfd88e06702517d823154d63168d81fbbc35ff05d79ab` |
| `manifest.json` | 2,560 | `df84679d17cc78c12f2ae2d322fb2df53fe7a568e5a0bee7daefd25e53ecc196` |
| `manifest-3273853.json` | 3,337 | `3e5306804736b7fd545d74584943251f9239897b4adb0ec29803f3126f517dfa` |
| `patch-3273853-r6.zip` | 227,904,003 | `15783d1f52e0109aea49118328e3493cca4292c104ae897dc659af71b8ad288b` |

The release page is authoritative; these are the uploaded bytes.

## Upgrade

0.1.2 and newer self-update to 0.1.5. 0.1.1 and older cannot follow the
`release-assets.githubusercontent.com` redirect and must download 0.1.5 manually once. Restore the
five game files and the two external font bundles before installing r6; Gate B compares the true
`before_size`/`before_sha256` pairs.

## Verification

- `build.cmd`: 882 checks, 0 failed; CTest `core` passes.
- `font-a00.r` and `font-b00.r` keep their original sizes (4,517,899 and 4,550,495) and CRC32; only
  the Cyrillic advances change.
- `startup.r` keeps its 58,707,878-byte size; `ast_b00_origin_SDF` points at the MiSans Font.
- The official launcher's `checkStat` compares sizes only; all checked files keep their official
  sizes. Full repair uses CRC-64 and would still notice `startup.r`; do not run full repair on a
  patched tree.
