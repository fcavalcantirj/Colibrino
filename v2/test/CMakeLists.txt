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
