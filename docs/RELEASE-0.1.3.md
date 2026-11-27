[English](RELEASE-0.1.3.md) · [Русский](RELEASE-0.1.3.ru.md)

# BLUEWARD 0.1.3

BLUEWARD 0.1.3 corrects the attribution line in Settings and refreshes the source checks and
documentation. Patch handling, the manifest schema and the five r3 payloads are unchanged.

## What changed

- The About line in Settings names `Yostar` and `Manjuu Co., Ltd.`.
- The source scan checks that the product never calls the GitHub API host and carries no compiled
  resource revision.
- The test-gap page describes the public release state and the `release-assets.githubusercontent.com`
  redirect.

## Artifact

| File | Size | SHA-256 |
| --- | ---: | --- |
| `BLUEWARD.exe` | 5,094,400 | `15dfc501bd8cba05877c6ba8069f8ebca5713013224fe20a61e86a375ef73778` |
| `manifest.json` | 2,561 | `85a5e2133b8fe1443c5a5ed04da0596d2bcbf24bdd0b2274ac88dd4009cc1cad` |
| `manifest-3273853.json` | 2,559 | `9720121e1099a5a962ae1998db466d01e6903899a7fa29237b0987b446f887f1` |

The release page is authoritative; these are the uploaded bytes.

## Upgrade

0.1.2 can self-update to 0.1.3. 0.1.1 and older cannot follow the
`release-assets.githubusercontent.com` redirect, so those users must download 0.1.3 manually once.

## Verification

- `build.cmd`: 865 checks, 0 failed; CTest `core` passes.
- The binary contains `Manjuu`, no GitHub API host and no compiled resource revision.
- The five before/after pairs and the `patch-3273853.zip` archive are unchanged from r3.
