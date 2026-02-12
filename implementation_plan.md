# BitGlide — Implementation Plan

**BitGlide** is an adaptive bitrate (ABR) controller for HLS streams, written in C++17 for Linux.
It parses HLS master/media playlists, estimates available bandwidth in real time, picks the
rendition to download for each segment, prefetches segments ahead of the playhead with a
configurable lookahead, and manages a playback buffer that is drained by a simulated player clock.
Python and Bash harnesses provide a bandwidth-throttled HLS origin and scripted network scenarios.

## Key design decisions

- **No heavy dependencies.** The core only needs a C++17 compiler, CMake and POSIX (pthreads,
  BSD sockets). HTTP/1.1 is implemented directly over sockets (plain `http://` and `file://`);
  HTTPS is out of scope — tests run against a local origin.
- **Threads are `pthread`s** wrapped in a tiny RAII `Thread` class so the multi-threading model
  is explicit: N prefetch workers, 1 playback-clock thread, 1 controller thread.
- **Pluggable ABR policy** behind an `AbrAlgorithm` interface: throughput-based, buffer-based
  (BBA-style reservoir/cushion), and a hybrid (default) with switch hysteresis.
- **Bandwidth estimation** from per-segment download samples: EWMA (fast + slow, take min, as
  in shaka/hls.js) and sliding-window harmonic mean.
- **Simulated playback.** No decoding — the player clock consumes segment durations from the
  buffer in (optionally scaled) real time, records stalls and quality switches.
- **Self-contained unit tests** via a minimal header-only test framework (no GoogleTest needed),
  run through CTest.

## Phases

### Phase 1 — Scaffolding and HLS playlist parsing
Deliverables: CMake project, core types, URL resolution, M3U8 master + media playlist parser.
Files: `CMakeLists.txt`, `include/bitglide/types.h`, `include/bitglide/url.h`, `src/url.cpp`,
`include/bitglide/playlist.h`, `src/playlist.cpp`, `include/bitglide/log.h`, `src/log.cpp`.

### Phase 2 — Bandwidth estimation and ABR algorithms
Deliverables: thread-safe bandwidth estimator (dual EWMA + harmonic mean), ABR interface with
throughput, buffer-based and hybrid policies.
Files: `include/bitglide/bandwidth_estimator.h`, `src/bandwidth_estimator.cpp`,
`include/bitglide/abr.h`, `src/abr.cpp`.

### Phase 3 — Networking and buffer management
Deliverables: POSIX thread wrapper, minimal HTTP/1.1 client with timing, `file://` fetcher,
thread-safe segment buffer (ordered by sequence number, capacity bound, condition variables),
playback clock thread that drains the buffer and records stalls.
Files: `include/bitglide/thread.h`, `include/bitglide/http_client.h`, `src/http_client.cpp`,
`include/bitglide/segment_buffer.h`, `src/segment_buffer.cpp`, `include/bitglide/player.h`,
`src/player.cpp`.

### Phase 4 — Prefetcher, controller, CLI
Deliverables: worker-pool prefetcher with configurable lookahead window, controller loop that
glues estimator + ABR + prefetcher + player, session metrics (JSON report), CLI binary.
Files: `include/bitglide/config.h`, `src/config.cpp`, `include/bitglide/prefetcher.h`,
`src/prefetcher.cpp`, `include/bitglide/controller.h`, `src/controller.cpp`,
`include/bitglide/metrics.h`, `src/metrics.cpp`, `src/main.cpp`.

### Phase 5 — Test harnesses
Deliverables: C++ unit tests (parser, estimator, ABR, buffer), Python throttled HLS origin
with scripted bandwidth profiles, Python scenario runner that asserts on the JSON report, Bash
scripts to build, generate content and run all scenarios.
Files: `tests/test_framework.h`, `tests/test_*.cpp`, `harness/hls_origin.py`,
`harness/gen_content.py`, `harness/run_scenarios.py`, `harness/profiles/*.json`,
`scripts/build.sh`, `scripts/run_tests.sh`, `scripts/scenario.sh`.

Afterwards: `.gitignore`, `README.md` with architecture diagram, run and test instructions.
