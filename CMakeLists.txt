cmake_minimum_required(VERSION 3.28)

project(Patronus CXX)

# MSVC x64 only -- fail fast with a clear message instead of letting a
# Ninja/GCC/x86 configure attempt fail confusingly deep in dependency setup.
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
  message(FATAL_ERROR
    "Patronus targets MSVC only. Configure with the 'windows' preset "
    "(cmake --preset windows), which selects the Visual Studio 17 2022 "
    "generator.")
endif()
if(NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
  message(FATAL_ERROR "Patronus is x64-only; configure with -A x64 (the 'windows' preset already does this).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # /permissive- territory: no GNU-style extensions

# Redirect FetchContent's download/source tree from the default
# build/_deps into external/, so fetched dependency sources are easy to
# find and inspect. external/ is gitignored -- nothing here is vendored,
# it's re-fetched on every clean configure.
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/external" CACHE PATH "" FORCE)

include(cmake/CompilerWarnings.cmake)
include(cmake/FetchAgilitySDK.cmake)
include(cmake/FetchDXC.cmake)
include(cmake/Dependencies.cmake)

# --- Agility SDK export + smoke test ------------------------------------
# Both files below are an explicitly authorized exception to the
# CLAUDE.md hard rule against writing RHI/graphics code -- see the file
# headers in src/rhi/ for details.
add_executable(PatronusSmokeTest
  src/rhi/AgilitySDKExports.cpp
  src/rhi/smoketest/main.cpp
)

target_compile_definitions(PatronusSmokeTest PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

# The Agility SDK package ships its own d3d12.h matching the exact
# redistributable version we deploy; take that over whatever ships in the
# ambient Windows SDK, which may be older.
target_include_directories(PatronusSmokeTest PRIVATE
  "${PATRONUS_AGILITY_SDK_INCLUDE_DIR}"
)

target_link_libraries(PatronusSmokeTest PRIVATE
  patronus_warnings
  d3d12.lib
  dxgi.lib
  dxguid.lib
  version.lib # GetFileVersionInfoW / VerQueryValueW, used to report the loaded Agility SDK DLL version
)

# --- Agility SDK runtime deployment -------------------------------------
# Copy the redistributable DLLs into a D3D12/ subfolder next to the exe
# after every build. See src/rhi/AgilitySDKExports.cpp for why this has
# to happen at all.
set(_agility_runtime_files
  "${PATRONUS_AGILITY_SDK_BIN_DIR}/D3D12Core.dll"
  "${PATRONUS_AGILITY_SDK_BIN_DIR}/d3d12SDKLayers.dll"
)
foreach(_optional_pdb
  "${PATRONUS_AGILITY_SDK_BIN_DIR}/D3D12Core.pdb"
  "${PATRONUS_AGILITY_SDK_BIN_DIR}/d3d12SDKLayers.pdb"
)
  if(EXISTS "${_optional_pdb}")
    list(APPEND _agility_runtime_files "${_optional_pdb}")
  endif()
endforeach()

add_custom_command(TARGET PatronusSmokeTest POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:PatronusSmokeTest>/D3D12"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_agility_runtime_files} "$<TARGET_FILE_DIR:PatronusSmokeTest>/D3D12"
  COMMENT "Deploying Agility SDK redistributable next to PatronusSmokeTest.exe"
  VERBATIM
)
