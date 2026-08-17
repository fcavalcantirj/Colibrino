# Optional clang-tidy on the core library only. Not a gate until the tool is
# installed on the workstation; the configuration lives in v2/.clang-tidy.
find_program(CV2_CLANG_TIDY_EXE NAMES clang-tidy)

function(cv2_enable_clang_tidy target)
  if(CV2_CLANG_TIDY_EXE)
    set_target_properties(${target} PROPERTIES
      C_CLANG_TIDY "${CV2_CLANG_TIDY_EXE};--warnings-as-errors=*")
  endif()
endfunction()

if(CV2_CLANG_TIDY_EXE)
  message(STATUS "clang-tidy found: ${CV2_CLANG_TIDY_EXE}")
else()
  message(STATUS "clang-tidy not found: skipped")
endif()
