# Compiler-as-typecheck flags for every first-party target (core + tests).
#
# -ffp-contract=off keeps float arithmetic bit-reproducible between the C core,
# the C++ differential test and the sticks3 reference (no fused multiply-add
# may silently change the EMA baseline). Vendored third-party code is built
# without this target and without -Werror (see third_party/CMakeLists.txt).
add_library(cv2_warnings INTERFACE)
target_compile_options(cv2_warnings INTERFACE
  -Wall
  -Wextra
  -Werror
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wdouble-promotion
  -Wvla
  -Wundef
  -ffp-contract=off
  $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes;-Wmissing-prototypes>
)
