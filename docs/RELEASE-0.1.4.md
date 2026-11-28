[English](RELEASE-0.1.4.md) · [Русский](RELEASE-0.1.4.ru.md)

# BLUEWARD 0.1.4

BLUEWARD 0.1.4 adds a Play button to the installed state. Patch `2026.11.28-4` repairs the startup
bundle so the translated client reaches the title screen.

## What changed

- After a successful install, the main card now shows `Играть` as the primary action. It starts
  `AzurPromiliaJP.exe` with the official `-start=azurpromilia_launcher` parameter from the configured
  game folder. Restore and reinstall remain available in the second row.
- The startup payload is rebuilt with the original UnityFS compression and encryption. The previous
  build forced LZMA/encrypted blocks into a bundle whose game loader expected the original layout,
  and the client stopped at `起動画面の読み込みに失敗しました。`.
- Patch `2026.11.28-4` changes only `startup.r` relative to r3; `vstring.bytes`, `static-text.r`,
  `table_data.bytes` and `onest-font.r` keep the r3 outputs.

## Artifact

| File | Size | SHA-256 |
| --- | ---: | --- |
| `BLUEWARD.exe` | 5,096,960 | `285bdcc60daea07a8805eb4cd4ec3d8d890464285dd8081e679f73f3eba2b45f` |
| `manifest.json` | 2,560 | `3313024282cc08f249ffb9fc27563c934df48308eef76ac1afec54bdb5ca5213` |
| `manifest-3273853.json` | 2,564 | `eb89482ab89c2d22147c99323901772e561059e6c5293d87b95aa96a3308a188` |
| `patch-3273853-r4.zip` | 233,942,380 | `e528341e5a3f9bf749f9fd0665d14a4502848d7c16263a32995329bc70230e17` |

The release page is authoritative; these are the uploaded bytes.

## Upgrade

0.1.2 and newer self-update to 0.1.4. 0.1.1 and older cannot follow the
`release-assets.githubusercontent.com` redirect and must download 0.1.4 manually once. The r4 patch
is selected through `manifest-3273853.json`; the compatibility `manifest.json` keeps the r1
`patch-3219598` block for the 0.1.0 bootstrap.

## Verification

- `build.cmd`: 875 checks, 0 failed; CTest `core` passes.
- The rebuilt `startup.r` preserves the original `UnityFS` flags and LZ4HC blocks; its CRC32 is
  balanced to the original slot.
- A manual r4 install on the installed 3273853 client reaches the Russian title screen
  (`Начать путешествие`); the game process stays up and no startup-bundle error is logged.
- The five before/after pairs and the ZIP size are unchanged; only the startup payload differs from
  r3.
