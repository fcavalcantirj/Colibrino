# ctest 'fixtures_manifest': the promoted trace set must be exactly what
# traces/MANIFEST.sha256 says it is.
#
#   cmake -DTRACES_DIR=<abs path> -P check_manifest.cmake
#
# Rules (all must hold):
#   * every manifest line is '<sha256>  <relative path>' and the file exists
#     with that digest;
#   * every traces/*.csv, *.labels.json, *.labels.tsv on disk is listed
#     (an unlisted fixture is an unpromoted fixture);
#   * every listed .csv starts with the canonical header line.
# An empty manifest with no fixture files passes trivially.
if(NOT DEFINED TRACES_DIR)
  message(FATAL_ERROR "TRACES_DIR not set")
endif()
set(manifest "${TRACES_DIR}/MANIFEST.sha256")
if(NOT EXISTS "${manifest}")
  message(FATAL_ERROR "missing ${manifest}")
endif()
set(expected_header "t_ms,t_us,gx_dps,gy_dps,gz_dps,ax_g,ay_g,az_g")

file(STRINGS "${manifest}" lines)
set(listed "")
set(errors 0)
foreach(line IN LISTS lines)
  if(line MATCHES "^[ \t]*$")
    continue()
  endif()
  if(NOT line MATCHES "^([0-9a-fA-F][0-9a-fA-F]+)  (.+)$")
    message(SEND_ERROR "malformed manifest line: '${line}'")
    math(EXPR errors "${errors}+1")
    continue()
  endif()
  set(digest "${CMAKE_MATCH_1}")
  set(rel "${CMAKE_MATCH_2}")
  string(TOLOWER "${digest}" digest)
  string(LENGTH "${digest}" digest_len)
  if(NOT digest_len EQUAL 64)
    message(SEND_ERROR "digest is not sha256 for ${rel}")
    math(EXPR errors "${errors}+1")
    continue()
  endif()
  set(path "${TRACES_DIR}/${rel}")
  if(NOT EXISTS "${path}")
    message(SEND_ERROR "listed fixture missing: ${rel}")
    math(EXPR errors "${errors}+1")
    continue()
  endif()
  file(SHA256 "${path}" actual)
  if(NOT actual STREQUAL digest)
    message(SEND_ERROR "digest mismatch: ${rel}")
    math(EXPR errors "${errors}+1")
  endif()
  if(rel MATCHES "\\.csv$")
    file(STRINGS "${path}" first LIMIT_COUNT 1)
    if(NOT first STREQUAL expected_header)
      message(SEND_ERROR "bad csv header in ${rel}: '${first}'")
      math(EXPR errors "${errors}+1")
    endif()
  endif()
  list(APPEND listed "${rel}")
endforeach()

file(GLOB on_disk RELATIVE "${TRACES_DIR}"
  "${TRACES_DIR}/*.csv" "${TRACES_DIR}/*.labels.json" "${TRACES_DIR}/*.labels.tsv")
foreach(rel IN LISTS on_disk)
  if(NOT rel IN_LIST listed)
    message(SEND_ERROR "fixture on disk but not in manifest: ${rel}")
    math(EXPR errors "${errors}+1")
  endif()
endforeach()

if(errors GREATER 0)
  message(FATAL_ERROR "fixtures_manifest: ${errors} problem(s)")
endif()
list(LENGTH listed count)
message(STATUS "fixtures_manifest: ${count} promoted file(s) verified")
