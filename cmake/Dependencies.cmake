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

FetchContent_MakeAvailable(tracy d3d12ma)

# Dear ImGui is not here: it ships no CMakeLists.txt of its own, so it needs
# a dozen lines of target setup rather than a declaration. It lives in
# cmake/DearImGui.cmake, included from the root CMakeLists.txt.
