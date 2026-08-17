# The oracle suite. Every executable is one ctest test with LABELS so a unit
# can be run in isolation (ctest -L arbiter, ctest -R blink_dsp, ...).
# The loop never edits this directory (see ../README.md denylist).

# cv2_add_test(<name> LABELS <l...> [SOURCES <extra...>] [CXX] [DEFS <d...>])
function(cv2_add_test name)
  cmake_parse_arguments(T "CXX" "" "LABELS;SOURCES;DEFS;LINK_OPTIONS" ${ARGN})
  if(T_CXX)
    set(main_src ${name}.cpp)
  else()
    set(main_src ${name}.c)
  endif()
  add_executable(${name} ${main_src} ${T_SOURCES})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE colibrino_v2 unity cv2_warnings)
  if(T_CXX)
    target_compile_features(${name} PRIVATE cxx_std_17)
  endif()
  if(T_DEFS)
    target_compile_definitions(${name} PRIVATE ${T_DEFS})
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "${T_LABELS}")
endfunction()

# ---- contracts -------------------------------------------------------------
cv2_add_test(contracts_static LABELS "contracts")
cv2_add_test(contracts_cxx CXX LABELS "contracts")

# ---- access-intent arbiter -------------------------------------------------
cv2_add_test(arbiter_negative LABELS "arbiter;intent")
cv2_add_test(arbiter_positive LABELS "arbiter;intent")
cv2_add_test(arbiter_property LABELS "arbiter;intent;property")
cv2_add_test(arbiter_rollover LABELS "arbiter;intent")

# ---- wire codec + profile --------------------------------------------------
cv2_add_test(wire_codec LABELS "wire")
cv2_add_test(profile_validate LABELS "profile")

# ---- fixtures manifest (cmake script) --------------------------------------
add_test(NAME fixtures_manifest
  COMMAND ${CMAKE_COMMAND} -DTRACES_DIR=${CMAKE_SOURCE_DIR}/traces
          -P ${CMAKE_SOURCE_DIR}/cmake/check_manifest.cmake)
set_tests_properties(fixtures_manifest PROPERTIES LABELS "fixtures;golden")

# ---- fixture-tooling contract ---------------------------------------------
# Pins the enums and the labels.tsv header shared by sticks3/tools (Python
# producer) and the C golden runner. Runs with the first python3 found;
# reported as skipped (exit 77) when python3 is absent, never as passed.
find_program(COLIBRINO_V2_PYTHON3 NAMES python3)
if(COLIBRINO_V2_PYTHON3)
  add_test(NAME labels_schema_consistency
    COMMAND ${COLIBRINO_V2_PYTHON3} ${CMAKE_SOURCE_DIR}/tools/tests/test_labels_schema.py)
else()
  add_test(NAME labels_schema_consistency COMMAND ${CMAKE_COMMAND} -E false)
  set_tests_properties(labels_schema_consistency PROPERTIES SKIP_RETURN_CODE 1)
endif()
set_tests_properties(labels_schema_consistency PROPERTIES LABELS "fixtures;contracts")

# ---- blink pipeline: differential vs the sticks3 v1 detector --------------
# Links the production v1 translation unit unchanged; both TUs get
# -ffp-contract=off via cv2_warnings so float results are comparable bit for
# bit. Vec3 comes from the header-only part of motion_controller.h; the
# motion controller itself is NOT linked.
set(CV2_STICKS3_DIR ${CMAKE_SOURCE_DIR}/../sticks3)
cv2_add_test(blink_differential CXX LABELS "blink;blink_pipeline;differential"
  SOURCES ${CV2_STICKS3_DIR}/src/imu_blink_detector.cpp)
target_include_directories(blink_differential PRIVATE ${CV2_STICKS3_DIR}/include)

# ---- blink-dsp / blink-code / pipeline synthetic oracles ------------------
cv2_add_test(blink_dsp_synth LABELS "blink;blink_dsp")
cv2_add_test(blink_code_synth LABELS "blink;blink_code")
cv2_add_test(blink_pipeline_synth LABELS "blink;blink_pipeline")

# ---- golden-trace oracles (rung 2) -----------------------------------------
# Read promoted fixtures from the SOURCE tree (traces/ is the oracle, hashed
# in MANIFEST.sha256). Without fixtures they SKIP (exit 77) under the
# developer preset and FAIL under -DCOLIBRINO_V2_ORACLE=ON.
foreach(golden IN ITEMS blink_dsp_golden blink_code_golden)
  cv2_add_test(${golden}
    LABELS "blink;golden;${golden}"
    SOURCES support/golden_runner.c
    DEFS "CV2_TRACES_DIR=\"${CMAKE_SOURCE_DIR}/traces\""
         "CV2_ORACLE=$<IF:$<BOOL:${COLIBRINO_V2_ORACLE}>,1,0>")
  set_tests_properties(${golden} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
