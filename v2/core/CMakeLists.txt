# The pure core: one static library, C11, warnings-as-errors, optional
# clang-tidy. No I/O, no clock, no allocation, no board types.
add_library(colibrino_v2 STATIC
  blink_code/code.c
  blink_dsp/channel_imu.c
  blink_dsp/impulse.c
  blink_pipeline/pipeline.c
  contracts.c
  intent/arbiter.c
  profile/crc32.c
  profile/loader.c
  wire/codec.c
)
target_include_directories(colibrino_v2 PUBLIC include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(colibrino_v2 PRIVATE cv2_warnings)
cv2_enable_clang_tidy(colibrino_v2)
