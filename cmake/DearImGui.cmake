# Dear ImGui as a self-contained target.
#
# Split out of Dependencies.cmake because ImGui is the one dependency we
# have to build ourselves (it ships no CMakeLists.txt), so its setup is a
# dozen lines rather than a FetchContent_Declare. Everything ImGui-specific
# lives here; the root CMakeLists.txt only does include() + link.
#
# Provides the target `imgui`: core + the Win32 platform backend + the D3D12
# renderer backend + the demo window. Docking and multi-viewport come from
# the branch, not from a compile flag.

include(FetchContent)

# Dear ImGui ships no version tags on the docking branch, so we pin an
# explicit commit instead of a moving branch ref -- otherwise every clean
# configure could silently pull in different ImGui code. Bump this SHA
# deliberately when you want to update.
#
# GIT_SHALLOW is deliberately FALSE here, unlike the other deps: a shallow
# fetch of a raw commit SHA (rather than a tag/branch ref) is a fragile
# pattern in general -- the docking branch moves fast and Dear ImGui is
# known to occasionally rewrite its history, so a shallow fetch of a
# specific SHA is more likely to fail as that SHA ages. A full clone is
# slightly slower but doesn't depend on the remote's shallow-fetch support
# for an unadvertised commit.
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        035c87ef847e5b6188713a6009f383f633b6043d # docking branch HEAD, pinned 2026-08-05
  GIT_SHALLOW    FALSE
)

FetchContent_MakeAvailable(imgui)

if(imgui_POPULATED AND NOT TARGET imgui)
  add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    # Kept deliberately: ImGui::ShowDemoWindow() is the fastest way to find
    # a widget, and the upstream README strongly recommends not disabling
    # it during development. It costs ~500 KB in a Debug build and is
    # dropped by the linker in Release if never referenced.
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
  )

  target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
  )

  # Compile the D3D12 backend against the same d3d12.h the app uses. The
  # COM interfaces ImGui touches are ABI-stable, so mixing headers would
  # work, but matching them removes the question entirely -- the same
  # reason D3D12MemoryAllocator's own CMakeLists does this. BEFORE, so the
  # Agility copy wins over the ambient Windows SDK.
  target_include_directories(imgui BEFORE PRIVATE
    "${PATRONUS_AGILITY_SDK_INCLUDE_DIR}"
  )

  target_compile_definitions(imgui PUBLIC
    UNICODE
    _UNICODE
    # The Win32 backend calls ImmGetContext/ImmSetCandidateWindow for IME
    # composition. Visual Studio auto-links imm32 via a #pragma, but state
    # it here so the dependency is visible rather than implied.
    IMGUI_ENABLE_WIN32_DEFAULT_IME_FUNCTIONS
  )

  target_link_libraries(imgui PUBLIC
    d3d12.lib
    dxgi.lib
    dxguid.lib
    imm32.lib
  )

  # Deliberately NOT linked against patronus_warnings: /W4 /WX applies to
  # our code only (see cmake/CompilerWarnings.cmake). A warning in a
  # dependency we don't control must never fail this build.
endif()
