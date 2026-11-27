[English](GAPS.md) · [Русский](GAPS.ru.md)

# Test gaps

`launcher-spec/TESTS.md` defines T-001…T-139. The CTest suite (`build\blueward_tests.exe`, 958 checks) automates the mechanical part. This page lists what it does not automate, so a green run is not read as full coverage.

## Covered headlessly

| Area | Test IDs (partial where noted) |
| --- | --- |
| Source token gate, no API host | T-002 |
| Config, Telegram, log rotation/format | T-013, T-014, T-016 |
| Page models and exact UI states | T-019, T-020 (model only), T-021 (model only) |
| Game discovery, path rejection, version detection, builtin/delta revision selection | T-024, T-025, T-026, T-027, T-029, T-030 |
| Latest/fallback manifest selection, 404 no-patch state, independent launcher update | T-017 (feed), T-032 (model) |
| Manifest schema, limits, targets, hashes, canonical URLs | T-035, T-036, T-037, T-038, T-039, T-040, T-041 |
| HTTP loopback, URL allowlist, redirect refusal, truncated body | T-043 (partial), T-045 (partial), T-046 (partial), T-052 (UA and encoding only) |
| Resumable download, 206/200/416, hash deletion | T-055, T-056 |
| Gate A, Gate B and exact gate text, managed reinstall | T-065, T-066, T-067, T-068, T-070, T-071, T-072, T-073 (partial) |
| ZIP container, traversal, extraction, payload identity | T-079, T-080, T-081, T-082, T-083, T-084, T-086, T-087 |
| Process refusal, install, rollback, install state | T-090, T-091, T-094 (partial), T-096, T-099, T-102, T-103 |
| Restore, idempotence, partial retry, running refusal | T-108, T-109, T-111, T-112, T-113 |
| Self-update versioning, handoff, rollback, cleanup | T-117, T-121, T-122, T-123 (partial) |
| SHA-256 vectors and progress | T-131 |

## Not automated

| Test IDs | What is missing | Why |
| --- | --- | --- |
| T-001, T-003, T-005 | PE headers, imports, resource sizes, `strings` scans | Run by the release checklist against `dist\BLUEWARD.exe`; not part of CTest. Verified for this build: PE32+ GUI x64, static CRT, `RT_RCDATA` 101 = 4,128,422 bytes, version block correct, zero banned strings. |
| T-004 | Icon visual review and donor-hash comparison | Visual; the icon has 16/24/32/48/64/128/256 px, all 32 bpp. |
| T-006, T-007, T-008, T-009, T-018 | Video loop/audio, gradient fallback, DPI, keyboard focus, window drag | Need a desktop session; see the manual protocol. |
| T-010, T-012, T-028 | Telegram launch, asset provenance review, folder picker | Desktop actions. |
| T-011 | Full message-catalog iteration | The gate strings are tested exactly; the rest of the catalog is reviewed by reading `src/core/messages.h`. |
| T-015 | Registry-write monitoring | Instrumentation only. The code calls no registry API. |
| T-017, T-092 | Second-instance activation and per-game mutex contention | Unit test covers the named mutex; the UI activation and 30 s wait need two real processes. |
| T-022 | Process watcher updating the idle UI | The process probe is tested; the polling UI is manual. |
| T-023, T-119, T-120 | Real UAC, elevated/non-elevated fixtures | The `asInvoker` read path is tested; a `requireAdministrator` fixture and real tokens are manual. |
| T-031, T-032, T-033, T-034 | Revision display on the real tree, game-version preflight, installed status, read-only idempotence | Partially covered by models and synthetic trees; the real tree is at delta 3273853, but no r3 manifest is published yet. |
| T-042, T-044 | Manifest revision code path, manifest SHA-256 in `install.json` | Revisions 3075915, 3219598 and 3273853 are accepted as data; the state file carries `manifest_sha256`, but no test asserts the saved value yet. |
| T-047, T-051 | TLS negative matrix, connect/body stall timeouts | Would need a TLS loopback with generated certificates and a stalling server; WinHTTP keeps validation on. |
| T-048, T-049, T-050 | Five-hop chain, loop detection, 429/503 `Retry-After`, 5xx retry counts | Redirect allowlist/refusal is tested; retries exist but are not exercised by a status-sequence server except the loopback cases. |
| T-053, T-054, T-057 | Exact 256 KiB manifest cap, launcher/archive caps, lying `Content-Length` | Streaming caps are implemented; the tests cover a truncated body but not every boundary. |
| T-058, T-125 | Capture-proxy host/path audit | Instrumentation only. |
| T-059, T-060, T-061, T-062, T-063, T-064, T-069, T-074, T-075 | Exact/fallback/ambiguity combinations, reparse fixtures, post-download recheck, full zero-write snapshot | The resolver and gate are implemented and partly tested; creating junctions and a deterministic mid-download mutation needs more fixtures. |
| T-076, T-077, T-078, T-085 | One-byte-short/long archive, cancel at 40/90 %, declared-size overrun | Download/verify logic is implemented; boundary and cancel-throttle cases are manual. |
| T-088, T-089 | Staging cleanup and write-path instrumentation | Cleanup is implemented; instrumentation is not. |
| T-093, T-095, T-097, T-098, T-100, T-101 | Disk-space refusal, backup conflict, `ReplaceFileW` failure, locked target, disk-full rollback, incomplete rollback | Not injected by the suite. |
| T-104, T-105, T-106 | Cancel after backup, kill recovery banner, 2 updates/s latency | Manual or a harness with a controllable clock. |
| T-107, T-116 | ACL deny and locked restore | Needs ACL setup and a held handle. |
| T-110, T-114, T-115 | Missing target restore, wrong-GR/corrupt state, reinstall after restore | Implementation exists; only the corrupt-backup and running cases are tested. |
| T-118 | Launcher asset hash/size boundary | The self-update verifies size and SHA-256; a loopback asset test is not in the suite. |
| T-124 | Self-update while busy or with a lower version | The busy guard and version comparison are implemented; the UI path is manual. |
| T-126, T-127, T-128, T-129, T-130 | Shell/process instrumentation, handle audit, reparse fixtures, path fuzzing | Code review and instrumentation, not CTest. |
| T-132, T-133, T-134, T-135, T-136, T-137, T-138, T-139 | Size/line count, startup/CPU/RAM, OS matrix, locale, CI, real-game acceptance, manifest cache | Manual, CI or the real game. |

## Hosting gap

The repository and the `launcher-v0.1.1` release are private. `curl` against the canonical latest asset URL returns HTTP 404 without authentication, and BLUEWARD sends no token by design. The manifest, launcher and patch URLs are correct, but the in-app update check and the patch download cannot fetch from a private release. Make the release assets public (or mirror the manifest and archive to a public host) before pointing a user at this build.

## Real-client gaps

- The local game has builtin `3075915` plus delta `3273853`; detection reports `3273853`. The no-patch state is expected until the packer publishes `manifest-3273853.json`; there is no compiled revision gate left to refuse the tree.
- The real 241 MB archive, GitHub redirect chain and Range support (T-048, T-055, T-076) have not been exercised; only loopback equivalents have.
- The five targets have not been accepted by the running game, and ACE/repair behaviour is unknown. Do not run the acceptance test while logged into the game.

