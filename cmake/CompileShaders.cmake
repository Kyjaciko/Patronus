# HLSL -> DXIL build rules using the DXC fetched by cmake/FetchDXC.cmake.
#
# Usage:
#
#   patronus_add_shader(<target>
#     SOURCE  shaders/particleshader.hlsl   # relative to the repo root
#     ENTRY   VSMain
#     PROFILE vs_6_6                        # any DXC target profile
#     OUTPUT  particle_shaders_VSMain       # basename; ".cso" is appended
#     [DEFINES FOO=1 BAR]                   # optional -D flags
#   )
#
# The .cso lands in the same directory as <target>'s executable, which is
# where GetAssetFullPath() in the app looks for it. Each call is one
# incremental custom command keyed on the .hlsl file, collected into a
# "<target>_shaders" custom target that <target> depends on, so a shader
# edit rebuilds only that shader and the VS generator shows the rule.
#
# Profiles: the project policy is Shader Model 6.6 (CLAUDE.md, ADR-0001).
# Anything lower silently loses SM 6.5/6.6 features the roadmap relies on
# (mesh shaders, ResourceDescriptorHeap, wave intrinsics in pixel shaders),
# so callers should pass *_6_6 unless they have a documented reason not to.
#
# Per-configuration flags:
#   Debug            -Zi -Qembed_debug -Od  : embedded PDB + no optimisation,
#                                             so PIX/RenderDoc show HLSL source
#   everything else  -O3                    : optimised, no debug info
# These are chosen with generator expressions so the multi-config Visual
# Studio generator gets the right set per configuration from one rule.
function(patronus_add_shader target)
  cmake_parse_arguments(ARG "" "SOURCE;ENTRY;PROFILE;OUTPUT" "DEFINES" ${ARGN})

  foreach(_required SOURCE ENTRY PROFILE OUTPUT)
    if(NOT ARG_${_required})
      message(FATAL_ERROR "patronus_add_shader(${target}): missing ${_required}")
    endif()
  endforeach()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "patronus_add_shader: '${target}' is not a target")
  endif()

  set(_source "${CMAKE_SOURCE_DIR}/${ARG_SOURCE}")
  if(NOT EXISTS "${_source}")
    message(FATAL_ERROR "patronus_add_shader(${target}): ${_source} does not exist")
  endif()

  # Not $<TARGET_FILE_DIR:${target}>: using the target's own file dir in an
  # OUTPUT that the same target depends on is a circular generator
  # expression ("No target ..." at generate time). Neither executable
  # overrides RUNTIME_OUTPUT_DIRECTORY, so this is the VS generator's
  # actual output location.
  set(_output "${CMAKE_BINARY_DIR}/$<CONFIG>/${ARG_OUTPUT}.cso")

  set(_defines "")
  foreach(_define IN LISTS ARG_DEFINES)
    list(APPEND _defines "-D${_define}")
  endforeach()

  add_custom_command(
    OUTPUT "${_output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/$<CONFIG>"
    COMMAND "${PATRONUS_DXC_EXECUTABLE}"
            -T ${ARG_PROFILE}
            -E ${ARG_ENTRY}
            $<$<CONFIG:Debug>:-Zi>
            $<$<CONFIG:Debug>:-Qembed_debug>
            $<$<CONFIG:Debug>:-Od>
            $<$<NOT:$<CONFIG:Debug>>:-O3>
            ${_defines}
            -Fo "${_output}"
            "${_source}"
    DEPENDS "${_source}"
    COMMENT "DXC ${ARG_SOURCE}:${ARG_ENTRY} (${ARG_PROFILE}) -> ${ARG_OUTPUT}.cso"
    VERBATIM
    COMMAND_EXPAND_LISTS
  )

  # One custom target per executable collects all its shader outputs. Listing
  # a custom command's OUTPUT as a source of a custom target is what makes
  # the command run as part of that target's build.
  set(_shader_target "${target}_shaders")
  if(NOT TARGET ${_shader_target})
    add_custom_target(${_shader_target})
    add_dependencies(${target} ${_shader_target})
    set_target_properties(${_shader_target} PROPERTIES FOLDER "Shaders")
  endif()
  target_sources(${_shader_target} PRIVATE "${_output}")
endfunction()
