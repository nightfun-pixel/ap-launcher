[English](RELEASE-0.1.1.md) В· [Р СѓСЃСЃРєРёР№](RELEASE-0.1.1.ru.md)

# BLUEWARD 0.1.1

BLUEWARD 0.1.1 removes the compiled resource baseline. The launcher now detects the installed revision, selects the matching manifest, and keeps self-update independent of patch availability.

## Mechanism

- The local revision is the highest numeric value from `AzurPromiliaJP_Data\StreamingAssets\.res\<pkg>\0release_<pkg>_<rev>.version` (builtin) and `AzurPromiliaJP_Data\.res\<pkg>\ManifestFiles\0release_<pkg>_<rev>.bytes` (delta). On builtin `3075915` plus delta `3273853` the result is `3273853` from the delta.
- The latest manifest is fetched from `releases/latest/download/manifest.json`. If its `patch.resource_revision` differs from the local revision, the launcher requests `releases/latest/download/manifest-<revision>.json`.
- A 404 on the per-revision manifest is the normal no-patch state: `РџР°С‚С‡ РґР»СЏ СЂРµРІРёР·РёРё <rev> РµС‰С‘ РЅРµ РІС‹РїСѓС‰РµРЅ`; install is disabled, the game is untouched, and the launcher block is still applied.
- `patch.files[]` is the only source of target paths, sizes and hashes. There is no compiled target table and no revision equality check.
- Gate B still requires all five before sizes and SHA-256 values to match before the first write; a mismatch leaves the game unchanged and prints the parameterized revision message.

## Assets in this release

| Asset | SHA-256 | Size |
| --- | --- | --- |
| `BLUEWARD.exe` | `47e6e6c8f66bcdea363ad0eff495e8ae4a074236adbbb19e24f2544b831c1f51` | 5,093,888 bytes |
| `manifest.json` | compatibility manifest: launcher 0.1.1, patch block still `patch-3219598` | вЂ” |

The executable embeds the 720p background (`RT_RCDATA` 101, 4,128,422 bytes). The source tree does not contain the clip; `res/bg` stays outside git.

## Verification

- `build.cmd`: 958 checks, 0 failed.
- Revision detection on the real local game tree: `valid=1 revision=3273853 source=delta packages=5`.
- The compatibility manifest parses with the new parser and with the 0.1.0 parser logic (`launcher=0.1.1`, `patch=3219598`, five files).
- The executable is unsigned; SmartScreen may warn on first run.

## Not in this release

`manifest-3273853.json` and `patch-3273853.zip` are not published here. The packer/r3 agent publishes them; this release only establishes the launcher and the feed fallback.
