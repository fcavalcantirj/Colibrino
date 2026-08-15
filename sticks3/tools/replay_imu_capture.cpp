// Replays a StickS3 `IMU,...` diagnostic log through the production detector.
//
// This utility intentionally shares the same ImuBlinkDetector implementation
// as firmware. It provides a reproducible host-side check that tuning changes
// reject stillness and ordinary head motion before they reach real USB HID.
#include <cstdio>
#include <cstring>

#include "colibrino/imu_blink_detector.h"

namespace {

struct StageResult {
  const char* name;
  uint32_t samples = 0;
  uint32_t impulses = 0;
  uint32_t sequences = 0;
};

StageResult* findStage(StageResult (&stages)[3], const char* name) {
  for (auto& stage : stages) {
    if (std::strcmp(stage.name, name) == 0) {
      return &stage;
    }
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <device-monitor.log>\n", argv[0]);
    return 2;
  }

  std::FILE* input = std::fopen(argv[1], "r");
  if (input == nullptr) {
    std::perror(argv[1]);
    return 2;
  }

  StageResult stages[] = {{"KEEP_HEAD_STILL"}, {"BLINK_FIRMLY"},
                          {"MOVE_HEAD"}};
  colibrino::ImuBlinkDetector detector;
  StageResult* active = nullptr;
  char line[512];
  while (std::fgets(line, sizeof(line), input) != nullptr) {
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

    StageResult* stage = findStage(stages, stage_name);
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
    ++stage->samples;
    if (detector.update(static_cast<uint32_t>(milliseconds), gyro)) {
      ++stage->sequences;
    }
    stage->impulses = detector.acceptedImpulses();
  }
  std::fclose(input);

  for (const auto& stage : stages) {
    std::printf("%s samples=%lu impulses=%lu sequences=%lu\n", stage.name,
                static_cast<unsigned long>(stage.samples),
                static_cast<unsigned long>(stage.impulses),
                static_cast<unsigned long>(stage.sequences));
  }

  const bool passes = stages[0].sequences == 0 && stages[1].sequences >= 2 &&
                      stages[2].sequences == 0;
  std::printf("RESULT=%s\n", passes ? "PASS" : "NOT_PROVEN");
  return passes ? 0 : 1;
}
