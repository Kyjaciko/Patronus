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
include(cmake/DeployAgilitySDK.cmake)

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

patronus_deploy_agility_sdk(PatronusSmokeTest)

# --- D3D12HelloTriangle -------------------------------------------------
# Microsoft's sample from DirectX-Graphics-Samples, copied into src/core/ by
# the user for learning purposes -- not written or modified by Claude
# (CLAUDE.md hard rule). Everything below is build plumbing only.
#
# Agility SDK opt-in: the sample as Microsoft ships it embeds its own
# D3D12SDKVersion / D3D12SDKPath exports hardcoded to the version that
# sample was written against (618), which does not track the version this
# project fetches and deploys. A mismatch is a hard runtime failure --
# D3D12CreateDevice returns 0x887E0003 D3D12_ERROR_INVALID_REDIST -- so the
# sample's copy was removed and it links the project's single definition in
# src/rhi/AgilitySDKExports.cpp instead (619), same as PatronusSmokeTest --
# one export, matching the one version FetchAgilitySDK.cmake deploys.
add_executable(D3D12HelloTriangle WIN32
  src/core/main.cpp
  src/core/stdafx.cpp
  src/core/DXSample.cpp
  src/core/Win32Application.cpp
  src/core/D3D12HelloTriangle.cpp
  src/rhi/AgilitySDKExports.cpp
)

# VS2022's toolset defaults ConformanceMode (/permissive-) to on when a
# project doesn't set it explicitly, which rejects this sample's
# &CD3DX12_HEAP_PROPERTIES(...)-style address-of-temporary pattern -- a
# long-standing MSVC extension the official sample relies on and was
# written against (CXX_EXTENSIONS alone doesn't override this for MSVC).
# This is a build conformance-mode choice, not a change to the sample's
# code.
target_compile_options(D3D12HelloTriangle PRIVATE /permissive)

target_compile_definitions(D3D12HelloTriangle PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
)

target_include_directories(D3D12HelloTriangle PRIVATE
  "${PATRONUS_AGILITY_SDK_INCLUDE_DIR}"
  # One level up from the include dir above: the sample's stdafx.h does
  # #include "include/d3dx12/d3dx12.h" (relative, with a literal "include/"
  # prefix). Adding the parent dir here makes that resolve to the fetched
  # Agility SDK's copy of d3dx12.h without touching the sample.
  "${PATRONUS_AGILITY_SDK_INCLUDE_DIR}/.."
)

target_link_libraries(D3D12HelloTriangle PRIVATE
  d3d12.lib
  dxgi.lib
  dxguid.lib
  d3dcompiler.lib
  shell32.lib # CommandLineToArgvW, used in Win32Application.cpp
)

patronus_deploy_agility_sdk(D3D12HelloTriangle)

# dxil.dll is the DXIL shader-bytecode validator the D3D12 runtime needs at
# PSO-creation time for SM6.x shaders (this DXC version always emits DXIL --
# see comment below, it promotes anything below SM6.0 up to 6.0 rather than
# emitting legacy DXBC). Deployed into the same D3D12/ subfolder as
# D3D12Core.dll on the theory that D3D12Core.dll looks for it relative to
# its own location -- NOT CONFIRMED: deploying it here, and separately next
# to the exe, both left the sample crashing identically (unhandled C++
# exception during LoadPipeline/LoadAssets, exact call site not yet
# isolated -- see docs/devlog for the current status of that investigation).
# Kept here as a plausible, harmless prerequisite either way.
add_custom_command(TARGET D3D12HelloTriangle POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${PATRONUS_DXC_BIN_DIR}/dxil.dll"
          "$<TARGET_FILE_DIR:D3D12HelloTriangle>/D3D12"
  COMMENT "Deploying dxil.dll into D3D12/ alongside D3D12Core.dll"
  VERBATIM
)

# shaders.hlsl is compiled to the .cso files D3D12HelloTriangle.cpp reads
# at startup (GetAssetFullPath resolves to the exe's own directory), using
# our fetched DXC (cmake/FetchDXC.cmake).
#
# Target profile is vs_5_1/ps_5_1, matching what this vendored sample's
# ReadDataFromFile-based design was originally built around (classic
# DirectX-Graphics-Samples pattern: precompiled bytecode, not compiled at
# runtime) rather than the project's general SM 6.6 policy (CLAUDE.md) --
# though note this particular DXC version promotes any profile below 6.0
# up to 6.0 regardless (see the "Promoting older shader model profile"
# build warning), so it's still emitting DXIL, just at 6.0 instead of 6.6.
# This target-profile choice is a build-flag decision, not a change to
# shaders.hlsl itself.
set(_hello_triangle_vs_cso "${CMAKE_BINARY_DIR}/$<CONFIG>/shaders_VSMain.cso")
set(_hello_triangle_ps_cso "${CMAKE_BINARY_DIR}/$<CONFIG>/shaders_PSMain.cso")

# Deliberately NOT using $<TARGET_FILE_DIR:D3D12HelloTriangle> for the
# outputs above: since these custom commands' outputs are made a build
# dependency of that same target below, referencing the target's own file
# dir in the OUTPUT path creates a circular generator-expression dependency
# CMake can't resolve ("No target D3D12HelloTriangle" at generate time).
# Neither target overrides RUNTIME_OUTPUT_DIRECTORY, so this is the VS
# generator's actual default output location -- same place
# PatronusSmokeTest.exe already lands.
add_custom_command(
  OUTPUT "${_hello_triangle_vs_cso}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/$<CONFIG>"
  COMMAND "${PATRONUS_DXC_EXECUTABLE}" -T vs_5_1 -E VSMain
          -Fo "${_hello_triangle_vs_cso}" "${CMAKE_SOURCE_DIR}/src/core/shaders.hlsl"
  DEPENDS "${CMAKE_SOURCE_DIR}/src/core/shaders.hlsl"
  COMMENT "Compiling shaders.hlsl:VSMain (vs_5_1)"
  VERBATIM
)
add_custom_command(
  OUTPUT "${_hello_triangle_ps_cso}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/$<CONFIG>"
  COMMAND "${PATRONUS_DXC_EXECUTABLE}" -T ps_5_1 -E PSMain
          -Fo "${_hello_triangle_ps_cso}" "${CMAKE_SOURCE_DIR}/src/core/shaders.hlsl"
  DEPENDS "${CMAKE_SOURCE_DIR}/src/core/shaders.hlsl"
  COMMENT "Compiling shaders.hlsl:PSMain (ps_5_1)"
  VERBATIM
)
add_custom_target(D3D12HelloTriangleShaders DEPENDS "${_hello_triangle_vs_cso}" "${_hello_triangle_ps_cso}")
add_dependencies(D3D12HelloTriangle D3D12HelloTriangleShaders)
