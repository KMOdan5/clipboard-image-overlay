# Plugin bootstrap module

include_guard(GLOBAL)

# Map fallback configurations for optimized build configurations
# gersemi: off
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO
    RelWithDebInfo
    Release
    MinSizeRel
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL
    MinSizeRel
    Release
    RelWithDebInfo
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELEASE
    Release
    RelWithDebInfo
    MinSizeRel
    None
    ""
)
# gersemi: on

# Prohibit in-source builds
if("${CMAKE_CURRENT_BINARY_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  message(
    FATAL_ERROR
    "In-source builds are not supported. "
    "Specify a build directory via 'cmake -S <SOURCE DIRECTORY> -B <BUILD_DIRECTORY>' instead."
  )
  file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeCache.txt" "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles")
endif()

# Add common module directories to default search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)

string(JSON _name GET ${buildspec} name)
string(JSON _website GET ${buildspec} website)
string(JSON _author GET ${buildspec} author)
string(JSON _email GET ${buildspec} email)
string(JSON _version GET ${buildspec} version)
string(JSON _bundleId GET ${buildspec} platformConfig macos bundleId)

set(PLUGIN_AUTHOR ${_author})
set(PLUGIN_WEBSITE ${_website})
set(PLUGIN_EMAIL ${_email})
set(PLUGIN_VERSION ${_version})

# clipboard-image-overlay: buildspec.json の beta が true なら、画面とログに出す版を「X.Y.Z-beta」にする。
# version は数字だけ(X.Y.Z)のまま使う(Windows のファイル情報や macOS のインストーラーが数字だけを求めるため)。
string(JSON _beta ERROR_VARIABLE _beta_error GET ${buildspec} beta)
if(_beta_error)
  set(_beta OFF)
endif()
if(_beta)
  set(PLUGIN_DISPLAY_VERSION "${_version}-beta")
else()
  set(PLUGIN_DISPLAY_VERSION "${_version}")
endif()
unset(_beta)
unset(_beta_error)

# clipboard-image-overlay: タグでビルドするとき(GitHub Actions)、タグと buildspec.json が食い違っていたら止める。
# version や beta の直し忘れのまま配らないようにするため。
if("$ENV{GITHUB_REF_TYPE}" STREQUAL "tag" AND NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "${PLUGIN_DISPLAY_VERSION}")
  message(
    FATAL_ERROR
    "Tag '$ENV{GITHUB_REF_NAME}' does not match buildspec.json (version + beta = '${PLUGIN_DISPLAY_VERSION}')"
  )
endif()
set(MACOS_BUNDLEID ${_bundleId})

string(REPLACE "." ";" _version_canonical "${_version}")
list(GET _version_canonical 0 PLUGIN_VERSION_MAJOR)
list(GET _version_canonical 1 PLUGIN_VERSION_MINOR)
list(GET _version_canonical 2 PLUGIN_VERSION_PATCH)
unset(_version_canonical)

include(buildnumber)
include(osconfig)

# Allow selection of common build types via UI
if(NOT CMAKE_GENERATOR MATCHES "(Xcode|Visual Studio .+)")
  if(NOT CMAKE_BUILD_TYPE)
    set(
      CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING
      "OBS build type [Release, RelWithDebInfo, Debug, MinSizeRel]"
      FORCE
    )
    set_property(
      CACHE CMAKE_BUILD_TYPE
      PROPERTY STRINGS Release RelWithDebInfo Debug MinSizeRel
    )
  endif()
endif()

# Disable exports automatically going into the CMake package registry
set(CMAKE_EXPORT_PACKAGE_REGISTRY FALSE)
# Enable default inclusion of targets' source and binary directory
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
