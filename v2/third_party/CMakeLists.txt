# Vendored third-party code: built without cv2_warnings (no -Werror) and
# exposed as a SYSTEM include so its headers never trip first-party warnings.
# Only unity/src is on the include path (unity/VERSION would otherwise shadow
# the C++ <version> header on case-insensitive filesystems).
add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity SYSTEM PUBLIC unity/src)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE UNITY_SUPPORT_64)
