set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(JSON_BuildTests         OFF CACHE INTERNAL "")
set(JSON_Install            OFF CACHE INTERNAL "")
set(SPIRV_REFLECT_STATIC_LIB  ON  CACHE INTERNAL "")
set(SPIRV_REFLECT_EXECUTABLE  OFF CACHE INTERNAL "")
set(SPIRV_REFLECT_BUILD_TESTS OFF CACHE INTERNAL "")
set(SPIRV_REFLECT_EXAMPLES    OFF CACHE INTERNAL "")
set(SPIRV_REFLECT_INSTALL    OFF CACHE INTERNAL "")
set(GLSLANG_TESTS           OFF CACHE INTERNAL "")
set(GLSLANG_ENABLE_INSTALL  OFF CACHE INTERNAL "")
set(SKIP_GLSLANG_INSTALL    ON  CACHE INTERNAL "")
set(ENABLE_GLSLANG_BINARIES OFF CACHE INTERNAL "")
set(ENABLE_SPVREMAPPER      OFF CACHE INTERNAL "")
set(ENABLE_GLSLANG_JS       OFF CACHE INTERNAL "")
set(ENABLE_HLSL             OFF CACHE INTERNAL "")
set(ENABLE_OPT              OFF CACHE INTERNAL "")
set(ARGPARSE_INSTALL        OFF CACHE INTERNAL "")
set(ARGPARSE_BUILD_TESTS    OFF CACHE INTERNAL "")
set(ARGPARSE_BUILD_SAMPLES  OFF CACHE INTERNAL "")
set(QJS_BUILD_EXAMPLES      OFF CACHE INTERNAL "")
set(QJS_BUILD_WERROR        OFF CACHE INTERNAL "")
set(QJS_BUILD_LIBC          OFF CACHE INTERNAL "")
set(QJS_BUILD_CLI_STATIC    OFF CACHE INTERNAL "")
set(QJS_BUILD_CLI_WITH_MIMALLOC OFF CACHE INTERNAL "")
set(QJS_BUILD_CLI_WITH_STATIC_MIMALLOC OFF CACHE INTERNAL "")

# ------------------------------------------------------------------
# Locally vendored dependencies.
# ------------------------------------------------------------------
add_subdirectory(glad)
add_subdirectory(nlohmann_json EXCLUDE_FROM_ALL)
add_subdirectory(argparse EXCLUDE_FROM_ALL)
add_subdirectory(spirv_reflect EXCLUDE_FROM_ALL)
add_subdirectory(glslang EXCLUDE_FROM_ALL)
add_subdirectory(quickjs_ng EXCLUDE_FROM_ALL)

if(TARGET qjs AND NOT TARGET quickjs::quickjs)
    add_library(quickjs::quickjs ALIAS qjs)
endif()

# Eigen is header-only for renderer usage.
add_library(eigen INTERFACE)
add_library(Eigen3::Eigen ALIAS eigen)
target_include_directories(eigen SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/eigen)

# miniaudio is consumed as <miniaudio.h> and <extras/stb_vorbis.c>.
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)
