// Replays StickS3 `IMU,...` diagnostic logs through the production detector.
//
// This utility intentionally shares the same ImuBlinkDetector implementation
// as firmware. It provides a reproducible host-side check that tuning changes
// reject stillness and ordinary head motion before they reach real USB HID.
//
// A device log may contain several guided probe sessions. Each session is
// reported separately with the same PASS predicate the firmware applies
// (still == 0, blink >= 2, head == 0), followed by an explicit summary:
//
//   SUMMARY sessions=N controls_clean=yes|no validation_passes=K
//
// Exit codes:
//   0  parsed at least one session and every control stage (still, head) is
//      clean in every session
//   1  a control-stage sequence occurred in at least one session
//   2  usage or parse failure, or zero sessions found
//   3  --require-validation N was given, controls are clean, but fewer than N
//      sessions PASS
//
// A clean exit 0 therefore means "parsed and controls clean", never
// "physically validated"; worn acceptance selects designated runs by ID in
// the host tooling and is not decided here.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "colibrino/imu_blink_detector.h"

namespace {

struct StageResult {
  const char* name;
  uint32_t samples = 0;
  uint32_t impulses = 0;
  uint32_t sequences = 0;
};

struct SessionResult {
  StageResult stages[3] = {{"KEEP_HEAD_STILL"}, {"BLINK_FIRMLY"}, {"MOVE_HEAD"}};
  bool passes() const {
    return stages[0].sequences == 0 && stages[1].sequences >= 2 &&
           stages[2].sequences == 0;
  }
  bool controlsClean() const {
    return stages[0].sequences == 0 && stages[2].sequences == 0;
  }
};

StageResult* findStage(SessionResult& session, const char* name) {
  for (auto& stage : session.stages) {
    if (std::strcmp(stage.name, name) == 0) {
      return &stage;
    }
  }
  return nullptr;
}

void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--events] [--require-validation [N]] "
               "<device-monitor.log>\n",
               argv0);
}

}  // namespace

int main(int argc, char** argv) {
  bool print_events = false;
  bool require_validation = false;
  unsigned long required_passes = 2;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--events") == 0) {
      print_events = true;
    } else if (std::strcmp(argv[i], "--require-validation") == 0) {
      require_validation = true;
      if (i + 1 < argc && argv[i + 1][0] != '-' &&
          std::strspn(argv[i + 1], "0123456789") == std::strlen(argv[i + 1])) {
        required_passes = std::strtoul(argv[++i], nullptr, 10);
      }
    } else if (argv[i][0] == '-') {
      usage(argv[0]);
      return 2;
    } else if (path == nullptr) {
      path = argv[i];
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  if (path == nullptr) {
    usage(argv[0]);
    return 2;
  }

  std::FILE* input = std::fopen(path, "r");
  if (input == nullptr) {
    std::perror(path);
    return 2;
  }

  std::vector<SessionResult> sessions;
  colibrino::ImuBlinkDetector detector;
  StageResult* active = nullptr;
  const char* previous_stage_name = nullptr;
  bool session_open = false;
  char line[512];
  while (std::fgets(line, sizeof(line), input) != nullptr) {
    // Explicit session boundary emitted by the firmware at every probe start.
    if (std::strncmp(line, "EVENT,IMU_PROBE_STARTED", 23) == 0) {
      sessions.emplace_back();
      session_open = true;
      active = nullptr;
      previous_stage_name = nullptr;
      continue;
    }

    unsigned long milliseconds = 0;
    unsigned long microseconds = 0;
    char stage_name[32] = {};
    colibrino::Vec3 gyro{};
    colibrino::Vec3 acceleration{};
    if (std::sscanf(line, "IMU,%lu,%lu,%31[^,],%f,%f,%f,%f,%f,%f",
                    &milliseconds, &microseconds, stage_name, &gyro.x, &gyro.y,
                    &gyro.z, &acceleration.x, &acceleration.y,
                    &acceleration.z) != 9) {
      continue;
    }

    // Fallback boundary for logs without the EVENT line: a still block that
    // follows a head block can only belong to a new session.
    const bool still_after_head =
        previous_stage_name != nullptr &&
        std::strcmp(previous_stage_name, "MOVE_HEAD") == 0 &&
        std::strcmp(stage_name, "KEEP_HEAD_STILL") == 0;
    if (!session_open || still_after_head) {
      sessions.emplace_back();
      session_open = true;
      active = nullptr;
    }
    SessionResult& session = sessions.back();

    StageResult* stage = findStage(session, stage_name);
    if (stage == nullptr) {
      continue;
    }
    if (stage != active) {
      detector.reset();
      active = stage;
      // Firmware feeds the detector during a three-second preparation phase,
      // but capture logs intentionally contain only measured-stage samples.
      // Seed a stationary two-second pre-roll so host replay starts at the
      // same quiet-gate boundary; it does not invent any counted sample.
      const unsigned long pre_roll_ms =
          milliseconds >= 2000 ? milliseconds - 2000 : 0;
      detector.update(static_cast<uint32_t>(pre_roll_ms), gyro);
    }
    previous_stage_name = stage->name;
    ++stage->samples;
    const uint32_t impulses_before = detector.acceptedImpulses();
    const bool sequence = detector.update(static_cast<uint32_t>(milliseconds), gyro);
    if (detector.acceptedImpulses() != impulses_before) {
      ++stage->impulses;
      if (print_events) {
        std::printf("EVENT,%lu,%s,%lu,IMPULSE\n",
                    static_cast<unsigned long>(sessions.size()), stage->name,
                    milliseconds);
      }
    }
    if (sequence) {
      ++stage->sequences;
      if (print_events) {
        std::printf("EVENT,%lu,%s,%lu,SEQUENCE\n",
                    static_cast<unsigned long>(sessions.size()), stage->name,
                    milliseconds);
      }
    }
  }
  std::fclose(input);

  if (sessions.empty()) {
    std::fprintf(stderr, "no IMU probe sessions found in %s\n", path);
    return 2;
  }

  unsigned long validation_passes = 0;
  bool controls_clean = true;
  for (size_t index = 0; index < sessions.size(); ++index) {
    const SessionResult& session = sessions[index];
    for (const auto& stage : session.stages) {
      std::printf("SESSION %lu %s samples=%lu impulses=%lu sequences=%lu\n",
                  static_cast<unsigned long>(index + 1), stage.name,
                  static_cast<unsigned long>(stage.samples),
                  static_cast<unsigned long>(stage.impulses),
                  static_cast<unsigned long>(stage.sequences));
    }
    const bool passes = session.passes();
    std::printf("SESSION %lu RESULT=%s\n", static_cast<unsigned long>(index + 1),
                passes ? "PASS" : "NOT_PROVEN");
    if (passes) {
      ++validation_passes;
    }
    if (!session.controlsClean()) {
      controls_clean = false;
    }
  }
  std::printf("SUMMARY sessions=%lu controls_clean=%s validation_passes=%lu\n",
              static_cast<unsigned long>(sessions.size()),
              controls_clean ? "yes" : "no", validation_passes);

  if (!controls_clean) {
    return 1;
  }
  if (require_validation && validation_passes < required_passes) {
    return 3;
  }
  return 0;
}
