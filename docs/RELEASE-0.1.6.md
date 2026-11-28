[English](RELEASE-0.1.6.md) · [Русский](RELEASE-0.1.6.ru.md)

# BLUEWARD 0.1.6

BLUEWARD 0.1.6 fixes a manifest-parser boundary bug in 0.1.5 and prepares the launcher for the
prefab-text patch. It does not change the r6 patch or the game files.

## What changed

- `patch.files[]` is accepted for the 5-, 7-, 8- and 10-payload sets. The duplicate-source table is
  now a `std::vector<bool>`; 0.1.5 used a fixed `seen[5]`, which wrote past the end on a
  seven-payload r6 manifest.
- The parser knows the future prefab names `settings-ui.r`, `story-brief.r` and
  `story-brief-builtin.r`, so the r7 manifest can be installed without another launcher rebuild.
- No network, install-layout or self-update behaviour changed.

## Artifact

| File | Size | SHA-256 |
| --- | ---: | --- |
| `BLUEWARD.exe` | 5,098,496 | `53dba972c064bd306d14de78da54c026542147d8658bc9aa62c6692218f4f6d2` |
| `manifest.json` | 2,560 | `352fe816070baf93fb3cfb6bb19e867e4f9edb1ff7176754925c25e2e0ec3964` |
| `manifest-3273853.json` | 3,337 | `cb64b42fc134b8c058496dc4ced2aa9d17a7537cfdcb8728abae1d67ddf4a443` |

The release page is authoritative; the two manifest hashes are regenerated with the 0.1.6 launcher
block and are shown there.

## Upgrade

0.1.2 and newer self-update to 0.1.6. 0.1.1 and older need one manual download. r6 remains the
current patch; the prefab-text r7 is a separate release.

## Verification

- `build.cmd`: 882 checks, 0 failed; CTest `core` passes.
- Seven-payload r6 manifest parses without the 0.1.5 out-of-bounds write.
- Five- and seven-payload legacy manifests still parse; eight- and ten-payload sets are reserved
  for r7/prefab releases.
