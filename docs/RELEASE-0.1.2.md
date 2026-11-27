[English](RELEASE-0.1.2.md) · [Русский](RELEASE-0.1.2.ru.md)

# BLUEWARD 0.1.2

BLUEWARD 0.1.2 restores downloads after GitHub moved release assets to
`release-assets.githubusercontent.com`, and stops treating a missing latest manifest as a network
failure.

## What changed

- The redirect allowlist now accepts the exact hosts `github.com`,
  `objects.githubusercontent.com` and `release-assets.githubusercontent.com`. HTTPS, port 443, no
  userinfo, no fragment and no IP literal are still required; a lookalike such as
  `release-assets.githubusercontent.com.evil` is refused.
- A 404 or 410 on `releases/latest/download/manifest.json` falls through to
  `manifest-<localRevision>.json`. If that file is also 404/410, the launcher shows the normal
  `Патч для ревизии <rev> ещё не выпущен` state and writes nothing. Transport, TLS and 5xx failures
  remain `E_NET`.
- The error card gives the primary action its own full-width row, so
  `Установить / Обновить перевод` is readable in full at 1100×720 instead of being clipped by the
  two-column grid.

## Artifact

| File | Size | SHA-256 |
| --- | ---: | --- |
| `BLUEWARD.exe` | 5,094,400 | `7c9aff698088a4a96d8b1fd3fcbca3da181e83f69eb218e2e8f5cd1964e746d2` |
| `manifest.json` | 2,561 | `e1cf7e26700d84740b03de9c78967d11d8472210457b105e91add222550b2375` |
| `manifest-3273853.json` | 2,559 | `51fbe4bc209aab447d5c026a01ffc8a2df9fc21c9092da2ac7068eeab5c4110d` |

The release page is authoritative; these are the uploaded bytes.

## Upgrade

0.1.1 and older cannot follow the `release-assets.githubusercontent.com` redirect, so their
self-update fails with `E_NET`. Download 0.1.2 manually once. From 0.1.2 onward self-update follows
the new host.

## Verification

- `build.cmd`: 1003 checks, 0 failed.
- The binary contains the `release-assets.githubusercontent.com` allowlist entry and no compiled
  resource revision.
- The five before/after pairs and the `patch-3273853.zip` archive are unchanged from r3.
