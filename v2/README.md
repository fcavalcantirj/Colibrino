# Colibrino v2 — pure core

Host-buildable C11 domain units for the Colibrino accessibility wearable,
behind fixed-size event contracts, with a Unity oracle suite. Nothing in this
tree touches hardware; the ESP32-S3 firmware lives in `../sticks3` and will
consume these headers later through their `extern "C"` guards.

**Branch-only until real fixtures exist.** This tree lands on `master` only
through a green integration branch (both presets below green, promoted
fixtures under `traces/`, and the owner's explicit merge grant). Never
auto-merge.

## Commands

All commands run from this directory (`v2/`).

```sh
# Developer preset: always green; golden-trace tests SKIP with an explicit
# "no fixtures promoted" message while traces/ is empty.
cmake --preset host && cmake --build build-host && ctest --test-dir build-host --output-on-failure

# Oracle gate: identical build with -DCOLIBRINO_V2_ORACLE=ON; zero promoted
# fixtures = the two golden tests FAIL (an empty oracle never passes).
cmake --preset host-oracle && cmake --build build-host-oracle && ctest --test-dir build-host-oracle --output-on-failure

# Variants: host-ninja (Ninja generator), host-asan (ASan + UBSan).
ctest --test-dir build-host -L arbiter      # one unit in isolation
ctest --test-dir build-host -R blink_dsp    # by name pattern
```

Toolchain: CMake >= 3.23, C11 (`C_EXTENSIONS OFF`), C++17 only for the two
test targets that consume the headers from C++. Flags on every first-party
target: `-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion
-Wdouble-promotion -Wvla -Wundef -ffp-contract=off` (+ `-Wstrict-prototypes
-Wmissing-prototypes` for C). `clang-tidy` runs on the core when installed
(`v2/.clang-tidy`), otherwise the configure step prints
`clang-tidy not found: skipped`. Vendored third-party code (Unity 2.6.1 under
`third_party/unity/`, see its `VERSION` file) is built without `-Werror`.

## Layout

```
core/include/colibrino/v2/   public contracts (C11, extern "C", no globals,
                             no clock reads, no allocation)
  common.h        cv2_ms_t + wrap-safe macros, producers, 16-byte event header,
                  IMU sample, cv2_header_validate
  access_intent.h intent event / context / config / state / action / faults
  profile.h       persisted profile blob (CRC32 = corruption detection only)
  wire.h          explicit little-endian codec for header + every event
  blink_dsp.h     blink-dsp event contract (IMPULSE / CANCEL)
  blink_code.h    blink-code event contract (CLICK_CANDIDATE)
  imu_motion.h    contract-only this round (types + prototypes)
  feel_defaults.h ALL tuning constants — DENYLISTED, human-owned
core/contracts.c  _Static_assert of every size/offset in the contracts
core/intent/      arbiter.c
core/profile/     loader.c, crc32.c
core/wire/        codec.c (+ internal le_bytes.h)
test/             Unity oracle suite (one executable = one ctest test, LABELS)
traces/           promoted fixtures: NAME.csv + NAME.labels.json +
                  NAME.labels.tsv, all hashed in MANIFEST.sha256
third_party/      vendored Unity
tools/            human-run helpers (manifest regeneration)
cmake/            warnings, optional clang-tidy, manifest checker
```

## Unit boundaries

| unit | entry | boundary |
|------|-------|----------|
| access-intent | `cv2_intent_arbitrate` | the ONLY path from an intent event to a host action; every fault → `kind NONE`, `release_all 1`, exact fault id |
| profile | `cv2_profile_load` | decode field-by-field, validate everything, publish atomically |
| wire | `cv2_*_encode/decode` | fixed sizes, versioned header, no raw struct copies, no partial writes |
| blink-dsp | (next commit) | ends at impulse events, never a click |
| blink-code | (next commit) | the click authority: double · pause · double, refractory |
| imu-motion | contract only | implementation deferred; differential vs sticks3 `MotionController` planned |

## Evidence labels

- `[REAL]` — a command that was actually run on this machine with the output
  quoted (build, ctest summary, replay of a retained device log).
- `[TEST]` — a synthetic test: constructed inputs, not recorded ones.
- Nothing in this tree is hardware validation. Worn acceptance is human-run on
  the StickS3 and recorded outside `v2/`.

## Denylist (the loop never edits these)

- `v2/test/**` — the oracle suite
- `v2/traces/**` — promoted fixtures and `MANIFEST.sha256`
- `v2/third_party/**` — vendored code
- `v2/tools/**` — capture / promotion helpers
- `v2/core/include/colibrino/v2/feel_defaults.h` — human-owned feel constants
- `sticks3/**` — the physical output path (HID/BLE, board firmware)

## Privacy

Fixtures under `traces/` are public artifacts: no MAC, hostname, IP, SSID,
absolute paths or time-of-day may appear in them or in test output.
`traces/candidates/` and `traces/private/` are git-ignored staging areas.
