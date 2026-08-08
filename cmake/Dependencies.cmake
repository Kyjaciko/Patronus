# Third-party dependencies, fetched via FetchContent. FETCHCONTENT_BASE_DIR
# is redirected to external/ (see root CMakeLists.txt) so the fetched
# sources are visible for browsing, but external/ is gitignored -- nothing
# here is vendored/committed, everything is re-downloaded at configure time.
include(FetchContent)

# D3D12MemoryAllocator builds a sample app by default that pulls in extra
# dependencies we don't need yet; we only want the library.
set(D3D12MA_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  tracy
  GIT_REPOSITORY https://github.com/wolfpld/tracy.git
  GIT_TAG        v0.13.1
  GIT_SHALLOW    TRUE
)

FetchContent_Declare(
  d3d12ma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git
  GIT_TAG        v3.2.0
  GIT_SHALLOW    TRUE
)

# Dear ImGui ships no version tags on the docking branch, so we pin an
# explicit commit instead of a moving branch ref -- otherwise every clean
# configure could silently pull in different ImGui code. Bump this SHA
# deliberately when you want to update.
#
# GIT_SHALLOW is deliberately FALSE here, unlike the other two deps: a
# shallow fetch of a raw commit SHA (rather than a tag/branch ref) is a
# fragile pattern in general -- the docking branch moves fast and Dear
# ImGui is known to occasionally rewrite its history, so a shallow fetch
# of a specific SHA is more likely to fail as that SHA ages. A full clone
# is slightly slower but doesn't depend on the remote's shallow-fetch
# support for an unadvertised commit.
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        035c87ef847e5b6188713a6009f383f633b6043d # docking branch HEAD, pinned 2026-08-05
  GIT_SHALLOW    FALSE
)

FetchContent_MakeAvailable(tracy d3d12ma)

# Dear ImGui has no CMakeLists.txt of its own, so we build it as a plain
# static library ourselves. Only the Win32 + DX12 backends are compiled,
# since that's the only platform this project targets.
FetchContent_MakeAvailable(imgui)
if(imgui_POPULATED AND NOT TARGET imgui)
  add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
  )
  target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
  )
endif()
