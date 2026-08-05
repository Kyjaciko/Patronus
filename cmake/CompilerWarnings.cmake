# Warning policy, scoped to first-party code only.
#
# This is an INTERFACE library, not a global CMAKE_CXX_FLAGS setting, on
# purpose: every target we author links against it explicitly, while
# third-party targets pulled in via FetchContent (Tracy, Dear ImGui,
# D3D12MemoryAllocator) never see /WX and can't fail our build over a
# warning in code we don't control.
add_library(patronus_warnings INTERFACE)

if(MSVC)
  target_compile_options(patronus_warnings INTERFACE
    /W4          # highest warning level that isn't mostly noise
    /WX          # warnings are errors, on our own code only (see above)
    /permissive- # reject MSVC's non-conforming-extensions mode
  )
endif()
