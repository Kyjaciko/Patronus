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
include(cmake/DearImGui.cmake) # after FetchAgilitySDK: uses PATRONUS_AGILITY_SDK_INCLUDE_DIR
include(cmake/DeployAgilitySDK.cmake)
include(cmake/CompileShaders.cmake)
include(cmake/DeployAssets.cmake)

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

# --- Profiling: frame timing log ----------------------------------------
# Harness plumbing (Claude-owned per CLAUDE.md): the sink that turns
# (frame, zone, ms) samples into the CSV tools/bench/bench_report.py reads.
# The GPU timestamp queries that produce the samples live in the renderer
# and are hand-written. Its own library so the test can link it without the
# app. Headers are included as "profiling/<name>.h" from the src/ root.
add_library(patronus_profiling STATIC
  src/profiling/FrameTimingWriter.cpp
)

target_include_directories(patronus_profiling PUBLIC src)

target_link_libraries(patronus_profiling PRIVATE patronus_warnings)

# --- Tests --------------------------------------------------------------
# Framework-free C++ tests live under tests/, mirroring the src/ layout,
# one executable per module. CI runs them next to the smoke test.
add_executable(PatronusFrameTimingWriterTest
  tests/profiling/FrameTimingWriterTest.cpp
)

target_link_libraries(PatronusFrameTimingWriterTest PRIVATE
  patronus_profiling
  patronus_warnings
)

# --- VFX system (skeleton) ----------------------------------------------
# src/renderer/vfx/ is the data-driven particle system described in
# docs/adr/0010-data-driven-emitters.md. The skeleton
# (structs, classes, empty functions) was written by Claude under a
# one-time authorised exception recorded in each file header; every body
# is written by the user. It is a static library so the format check and
# /W4 /WX apply to it from day one (src/core/ is still excluded from both,
# see .clang-format-ignore), and so D3D12HelloTriangle can adopt it
# incrementally.
add_library(patronus_vfx STATIC
  src/renderer/vfx/SpellAsset.cpp
  src/renderer/vfx/SpellInstance.cpp
  src/renderer/vfx/VfxResources.cpp
  src/renderer/vfx/VfxPipeline.cpp
  src/renderer/vfx/VfxSystem.cpp
)

target_compile_definitions(patronus_vfx PUBLIC
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

# Headers are included as "renderer/vfx/<name>.h" from the src/ root, so a
# file's path says where it lives without relative ../ chains.
target_include_directories(patronus_vfx PUBLIC
  src
  "${PATRONUS_AGILITY_SDK_INCLUDE_DIR}"
)

target_link_libraries(patronus_vfx
  PUBLIC
    D3D12MemoryAllocator # VfxResources.h holds D3D12MA::Allocation handles
    d3d12.lib
    dxguid.lib
  PRIVATE
    patronus_warnings
)

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
  src/platform/input/KeyboardEvent.cpp
  src/platform/input/Keyboard.cpp
  src/platform/input/MouseEvent.cpp
  src/platform/input/Mouse.cpp
  src/core/Win32Application.cpp
  src/core/GameObject.cpp
  src/core/GameObject3D.cpp
  src/core/Camera3D.cpp
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
  # The build configuration as a string literal, for the "config=" metadata
  # line of the benchmark CSV (tools/bench/README.md). Visual Studio is a
  # multi-config generator, so this has to be a generator expression;
  # CMAKE_BUILD_TYPE is empty here. CMake escapes the quotes for MSVC.
  PATRONUS_BUILD_CONFIG="$<CONFIG>"
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
  patronus_vfx
  patronus_profiling
  imgui # debug UI; brings its own include dirs and imm32/d3d12 links
  d3d12.lib
  dxgi.lib
  dxguid.lib
  d3dcompiler.lib
  shell32.lib # CommandLineToArgvW, used in Win32Application.cpp
  winmm
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

# Shaders live in shaders/ and are compiled to the .cso files
# D3D12HelloTriangle.cpp reads at startup (GetAssetFullPath resolves to the
# exe's own directory). See cmake/CompileShaders.cmake for the rule; every
# stage is SM 6.6 per CLAUDE.md / ADR-0001. The earlier vs_5_1/ps_5_1
# profiles were a leftover from the vendored sample and would have blocked
# mesh shaders, ResourceDescriptorHeap and pixel-shader wave intrinsics.
# Output basenames are the names the .cpp asks for; do not rename here
# without renaming there.
patronus_add_shader(D3D12HelloTriangle
  SOURCE shaders/shaders.hlsl ENTRY VSMain PROFILE vs_6_6 OUTPUT shaders_VSMain)
patronus_add_shader(D3D12HelloTriangle
  SOURCE shaders/shaders.hlsl ENTRY PSMain PROFILE ps_6_6 OUTPUT shaders_PSMain)
patronus_add_shader(D3D12HelloTriangle
  SOURCE shaders/computeshader.hlsl ENTRY CSMain PROFILE cs_6_6 OUTPUT shaders_CSMain)
patronus_add_shader(D3D12HelloTriangle
  SOURCE shaders/particleshader.hlsl ENTRY VSMain PROFILE vs_6_6 OUTPUT particle_shaders_VSMain)
patronus_add_shader(D3D12HelloTriangle
  SOURCE shaders/particleshader.hlsl ENTRY PSMain PROFILE ps_6_6 OUTPUT particle_shaders_PSMain)

# Baked assets (assets/) are copied next to the exe as assets/ so the app
# can load them via GetAssetFullPath(L"assets/...") regardless of the
# working directory. See cmake/DeployAssets.cmake.
patronus_deploy_assets(D3D12HelloTriangle)