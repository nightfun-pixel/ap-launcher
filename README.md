[English](README.md) · [Русский](README.ru.md)

# BLUEWARD

BLUEWARD is a Win32 + Direct2D launcher and patcher for the Japanese PC client of **Azur Promilia 1.0.2**. It replaces five resource files inside the game with a Russian translation archive published in this repository's releases, and puts the original files back on demand. It is an unofficial fan tool, not affiliated with Yostar or Manjuu. It uses no account, sends no telemetry, and does not interact with the anti-cheat.

Product version `0.1.6`; target game `1.0.2`. The resource revision is detected from the installed game, not compiled in: the launcher reads builtin and delta manifests and uses the highest revision it finds.

## How it works

The launcher is one x64 GUI executable, `BLUEWARD.exe`, built as C++20/Win32 with the static MSVC runtime. The shell is Direct3D 11 + Direct2D 1.1 + DirectWrite. Official release builds embed an H.264 MP4 (`RT_RCDATA` 101, 4,128,422 bytes) played through Media Foundation, with no audio stream selected. The clip is official promotional media and is **not stored in this repository**; a source-only build omits it and keeps the gradient background. The video pauses when the window is minimized or loses foreground and resumes when active.

The update feed is a schema-1 manifest at the canonical release URL. It names the launcher executable, one patch ZIP, and exactly five payloads with two identities each: the file that must already be at the target (`before_size`, `before_sha256`) and the file that replaces it (`size`, `sha256`). Nothing is written to the game until both identities and the local resource revision have been checked. If the latest manifest's `patch.resource_revision` differs from the local revision, the launcher fetches `manifest-<localRevision>.json` beside it before concluding that no patch exists. A 404 or 410 on the latest manifest falls through to that per-revision file as well; only transport, TLS and 5xx failures are network errors. The launcher block is parsed and offered independently, so a compatibility manifest may keep an older patch while shipping a newer client.

The publication convention is one Latest release with:

| Asset | Meaning |
| --- | --- |
| `manifest.json` | the current default manifest; its patch block is the newest patch that existing clients can parse |
| `manifest-<rev>.json` | the patch manifest for exactly resource revision `<rev>`; uploaded to the same Latest release |
| `BLUEWARD.exe` | the current launcher |
| `patch-<rev>` tag | the patch archive for `<rev>`, at `releases/download/patch-<rev>/patch-<rev>.zip` |

A revision that has no `manifest-<rev>.json` is a normal no-patch state: the launcher says `Патч для ревизии <rev> ещё не выпущен`, disables install, and writes nothing.

All user-visible strings are Russian and come from `src/core/messages.h`; error codes are stable and deterministic.

## Build

Toolchain used and verified: MSVC 19.44 (Visual Studio 2022 Build Tools), CMake 4.4, Ninja 1.13, Windows SDK 10.0.26100.

```bat
.\build.cmd
```

`build.cmd` runs `vswhere`, `vcvars64.bat`, configures CMake/Ninja in `Release` with the static CRT, builds `--clean-first`, runs `build\blueward_tests.exe`, and copies the result to `dist\BLUEWARD.exe`. A bare `cmake --build` outside a `vcvars64` shell fails to find the Windows headers; use the script. The background clip is not in the source tree: `build.cmd` detects its absence and builds the gradient-only variant. To reproduce the official artifact, place the clip at `res/bg/bg-720.mp4` (SHA-256 `3C57F682…C7015B`, 4,128,422 bytes) before building.

The build is warning-free with `/W4 /permissive- /utf-8`. The executable has no `VCRUNTIME140.dll`, `MSVCP140.dll` or `d3dcompiler_*.dll` dependency. Official release artifact with the clip: 5,094,400 bytes, SHA-256 `7C9AFF698088A4A96D8B1FD3FCBCA3DA181E83F69EB218E2E8F5CD1964E746D2`; a source-only build without the clip is smaller and has a different hash.

## Run

```text
dist\BLUEWARD.exe
```

On first start the launcher reads `%LOCALAPPDATA%\BLUEWARD\config.json`, auto-detects the game under the standard Yostar folders, and shows the game folder, client version and resource revision. If detection fails, the folder picker accepts the game root or its parent.

`--shoot <path>` renders the window for three seconds, writes a PNG, and exits. It is a development check; it does not install anything.

### Configuration

`%LOCALAPPDATA%\BLUEWARD\config.json`, schema 1, written atomically. Unknown keys are preserved. A damaged file is renamed to `config.bad-<timestamp>.json` and defaults are used.

| Key | Meaning | Default |
| --- | --- | --- |
| `schema` | must be `1` | `1` |
| `game_path` | game root | auto-detected |
| `language` | UI language | `"ru"` |
| `telegram_url` | `https://t.me/<name>` or `https://telegram.me/<name>`, 1–64 `[A-Za-z0-9_]` | `"REPLACE_ME"` (link disabled) |
| `video` | play the embedded background | `true` |
| `check_updates` | check the feed at startup | `true` |
| `window` | `x`, `y`, `w`, `h`, `maximized` | 900×620 |

### Log

`%LOCALAPPDATA%\BLUEWARD\logs\launcher.log`, UTF-8, one line per event, UTC ISO-8601 with milliseconds. Rotation happens before a write that would exceed 4,194,304 bytes: `launcher.log` → `launcher.1.log`, two files maximum. Query strings and `%USERPROFILE%` are removed/shortened. A write failure disables file logging for the session without failing the job.

## Patch targets

The five targets are read only from `patch.files[]` in the fetched manifest: `target`, `before_size`, `before_sha256`, `size`, and `sha256`. There is no compiled target table or baseline; a new resource revision needs a new manifest, not a new launcher. The parser requires exactly five entries, unique sources and targets, safe relative paths under `AzurPromiliaJP_Data/`, and sizes and hashes in range. Resolution tries each exact target path first. Only if it is absent, the launcher searches Res root A (`AzurPromiliaJP_Data\.res`) then Res root B (`AzurPromiliaJP_Data\StreamingAssets\.res`) for a regular file with the same basename; exactly one match across both roots is required. Reparse points, `.apbak` files, depth beyond 8, more than 200,000 entries and more than 15 seconds abort the resolution. The journal records whether the exact path or which root was used.

## Version gate

The local revision is the highest numeric revision found in either place:

| Source | Path |
| --- | --- |
| builtin | `AzurPromiliaJP_Data\StreamingAssets\.res\<pkg>\0release_<pkg>_<rev>.version` |
| delta | `AzurPromiliaJP_Data\.res\<pkg>\ManifestFiles\0release_<pkg>_<rev>.bytes` |

Each package contributes at most one candidate. A second release file, or a `.version` whose content disagrees with its filename, makes the result invalid rather than silently selecting an older revision. A builtin `3075915` plus a delta `3273853` therefore reports `3273853` from the delta. `AzurPromiliaJP_Data\.res` is optional. The game folder is still valid when it contains `AzurPromiliaJP.exe` and `AzurPromiliaJP_Data\StreamingAssets\.res`.

If the local revision has no manifest, the launcher shows `Патч для ревизии <rev> ещё не выпущен`, disables install, and writes nothing; self-update remains available because the launcher block is independent of the patch block. If a manifest exists, Gate B checks all five resolved targets in manifest order: size first, then SHA-256, against `before_*`. Any missing, wrong-size or wrong-hash file stops the job with:

```text
Сначала обновите игру до <rev> — запустите игру и пройдите загрузку на первом экране. Файлы не изменены.
```

and exactly one detail: `Не найден файл: <basename>.`, `Не совпадает размер: <basename> (ожидалось <n> Б, получено <m> Б).`, or `Не совпадает SHA-256: <basename>.`. A change detected after the download uses `Файл изменился во время установки: <basename>.`. A managed re-install may satisfy the gate from `install.json` plus a matching `.apbak`.

## Install transaction

1. Refuse if `AzurPromiliaJP.exe` or `AZUPRO_JP_Gamelauncher.exe` runs.
2. Download `patch.url` to `%LOCALAPPDATA%\BLUEWARD\downloads\patch-<revision>-<sha12>.part`, resuming with `Range`; verify size and SHA-256; rename to `.zip`.
3. Open the ZIP (1–6 entries, store/deflate only, no ZIP64, encryption, directories or symlinks, ASCII root names) and extract only the five allowlisted names into `%LOCALAPPDATA%\BLUEWARD\staging\<revision>-<version>-<sha12>\`. A sixth validated entry is ignored.
4. Verify each payload's size, SHA-256 and CRC.
5. Re-check the process gate and Gate B against the current files.
6. Create `<target>.apbak` next to each target once; an existing backup is reused only when it matches `before_*`.
7. Write `%LOCALAPPDATA%\BLUEWARD\backups\<YYYYMMDD-HHMMSS>\journal.json` before the first commit.
8. Replace each target through `<target>.blueward-new`, flush, hash, `ReplaceFileW` (or `MoveFileExW` fallback), verify the target, restore attributes.
9. On success write `%LOCALAPPDATA%\BLUEWARD\install.json`, mark the journal `committed`, delete the journal, staging and archive, and keep every `.apbak`.

Any failure after the first commit rolls back every changed target in reverse order from its `.apbak`, verifies `before_sha256`, restores attributes, removes the backups created by that attempt and deletes the journal. If rollback cannot restore a file, the journal is kept with status `rollback_failed` and the UI offers `Завершить откат`.

A second install of the same patch is a no-op with `Уже установлено`.

## Restore

`Вернуть оригинал` reads `install.json`; it refuses another game root, a running game, or a corrupt state. For each entry it first compares the target with `before_sha256`; a match needs no write. Otherwise it verifies the `.apbak` size and SHA-256, copies it atomically, verifies the target, and restores attributes. `.apbak` files are kept. All entries restored → `install.json` is deleted and the result is `Оригинальные файлы восстановлены.`. Partial failure → failed entries stay in `install.json`, result `E_RESTORE_PARTIAL`, and a retry handles exactly those entries. A second restore with no state is an informational `E_RESTORE_NONE` no-op.

## Self-update

Self-update is in-process; there is no helper executable. The new `BLUEWARD.exe` is downloaded from the canonical latest URL, verified against `launcher.size`/`launcher.sha256`, and its `RT_MANIFEST` is read as data to compare `requestedExecutionLevel` with the current elevation. The file is copied to `BLUEWARD.exe.new`, verified, the running image is renamed to `BLUEWARD.exe.old`, `.new` is moved into place, the single-instance mutex is released, the new image starts with the original command line, and the old process exits. A failure after the rename restores `.old`, deletes `.new`, reacquires the mutex and keeps the launcher running with a stable `E_SELFUPDATE_*` code. The next start deletes `.old` and `.new` with three 200 ms retries. 0.1.1 and older cannot follow GitHub's `release-assets.githubusercontent.com` redirect, so their self-update fails; those users must download 0.1.6 manually once. From 0.1.2 onward self-update works through that host.

## Network

Initial requests are only the canonical `https://github.com/nightfun-pixel/ap-launcher/...` release URLs: the latest manifest, `manifest-<revision>.json`, `BLUEWARD.exe`, and `patch-<revision>.zip`. Redirects are followed at most five times and only to `github.com`, `objects.githubusercontent.com` or `release-assets.githubusercontent.com`, HTTPS, port 443, no userinfo, no fragment; a signed query is allowed only on a redirect target. Hosts are matched exactly, so `release-assets.githubusercontent.com.evil` is refused. A loop is detected by the visited URL set. TLS certificate and host-name validation stay enabled; TLS 1.2 or newer is required. The client sends `GET`, `User-Agent: BLUEWARD/0.1.6 (Windows NT 10.0; Win64; x64)`, `Accept-Encoding: identity`, no cookies, no authorization, no referer. The manifest cap is 262,144 bytes; the archive cap is 2,147,483,648 bytes; one payload cap is 1,073,741,824 bytes. Network errors and 5xx are retried up to three times with 1/2/4 s backoff; 404 and 410 are not retried. The implementation never calls the GitHub API host.

## Tests

```powershell
build.cmd
& "C:\Program Files\CMake\bin\ctest.exe" --test-dir build --output-on-failure
```

Current result: **882 checks, 0 failed**. CTest name `core`. The suite runs headless, uses only `%TEMP%\blueward-tests-<pid>-<n>`, a loopback HTTP server and a synthetic `AZUPRO_JP` tree; it never opens or writes the real installed game. It covers:

| Area | Checks |
| --- | --- |
| JSON, SHA-256, manifest schema/limits/URLs/hashes/sizes, exact gate messages | `tests/test_json.cpp`, `test_sha256.cpp`, `test_manifest.cpp` |
| HTTP allowlist, redirect refusal, loopback 200/302/404/truncated body | `tests/test_http.cpp` |
| Resumable `.part`, 206/200/416, hash mismatch deletion, cancel, retry | `tests/test_download.cpp` |
| ZIP store/deflate, 5/6/7/4/0 entries, traversal, Unicode, directory, symlink, encryption, CRC, ZIP64, truncation, duplicates | `tests/test_zip.cpp` |
| Target resolution, fallback, ambiguity, revision gate, before gate, managed reinstall, install/rollback/restore, partial restore | `tests/test_installer.cpp` |
| Game detection, normalized paths, version detection, builtin/delta revision detection, process probes | `tests/test_gamepath.cpp` |
| Latest/fallback manifest selection, 404 latest and 404 fallback no-patch states, 5xx error, independent launcher update | `tests/test_feed.cpp` |
| Config defaults/round-trip/corrupt rename, Telegram validation, log rotation/format/sanitisation | `tests/test_config.cpp`, `test_log.cpp` |
| Self-update versioning, file handoff, rollback, cleanup, manifest level read | `tests/test_self_update.cpp` |
| Single instance, page models, exact UI states, geometry | `tests/test_single_instance.cpp`, `test_pages.cpp` |
| Source scan: no GitHub API host, no compiled revision | `tests/test_source_scan.cpp` |

The headless suite implements the mechanical subset; `docs/GAPS.md` lists the manual and instrumentation-only cases that are not automated, including the real-game acceptance run.

## Assets and licensing

| Asset | Source | Terms |
| --- | --- | --- |
| `res/blueward.ico` | Original BLUEWARD mark | project asset |
| Background clip | Official Azur Promilia JP site hero clip, re-encoded; **not stored in this repository** | © Manjuu Co., Ltd. / Yostar, Inc.; release build only, do not redistribute without permission |
| UI font | system Segoe UI | no font file shipped |
| `third_party/miniz` | miniz 2.2.0 | MIT; license in `third_party/miniz/LICENSE` |

The background clip is official promotional media, not a fan asset. It is **not distributed in this source tree**; official release binaries embed it, and source builds fall back to the gradient. See `docs/ATTRIBUTION.md`.

## Known gaps

- The local tree has builtin `3075915` and delta `3273853`; the launcher reports `3273853`, and `manifest-3273853.json` is published. Install and restore have still only been exercised on a synthetic tree; the real-game acceptance run is outstanding.
- The live window was built and resource-inspected, but the manual video/DPI/keyboard tests (T-006, T-008, T-009, T-018) have not been run on a desktop.
- TLS loopback with self-signed/expired/wrong-host certificates (T-047) and capture-proxy instrumentation (T-052, T-058, T-125) are not automated; WinHTTP keeps validation on by default.
- Elevation comparison is implemented and the `asInvoker` path is tested; a `requireAdministrator` fixture (T-120) and real UAC runs (T-023, T-119) are manual.
- Disk-space, per-game mutex contention, `ReplaceFileW` failure injection, ACL preservation, crash-kill recovery and UI responsiveness are not fully automated.
- The executable is unsigned; SmartScreen will warn on first run.
- The published 0.1.1 launcher cannot follow GitHub's release-assets redirect and cannot self-update; 0.1.6 must be downloaded manually once. The 0.1.2+ allowlist accepts only the documented `release-assets.githubusercontent.com` host, not a wildcard.

See `docs/GAPS.md` for the test-by-test list.


