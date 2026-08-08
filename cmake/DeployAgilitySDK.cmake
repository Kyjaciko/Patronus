# Copies the Agility SDK redistributable DLLs into a D3D12/ subfolder next
# to a target's exe after every build. See src/rhi/AgilitySDKExports.cpp for
# why this has to happen at all. Factored out because more than one
# executable target needs it (PatronusSmokeTest, D3D12HelloTriangle, ...).
function(patronus_deploy_agility_sdk target)
  set(_runtime_files
    "${PATRONUS_AGILITY_SDK_BIN_DIR}/D3D12Core.dll"
    "${PATRONUS_AGILITY_SDK_BIN_DIR}/d3d12SDKLayers.dll"
  )
  foreach(_optional_pdb
    "${PATRONUS_AGILITY_SDK_BIN_DIR}/D3D12Core.pdb"
    "${PATRONUS_AGILITY_SDK_BIN_DIR}/d3d12SDKLayers.pdb"
  )
    if(EXISTS "${_optional_pdb}")
      list(APPEND _runtime_files "${_optional_pdb}")
    endif()
  endforeach()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_runtime_files} "$<TARGET_FILE_DIR:${target}>/D3D12"
    COMMENT "Deploying Agility SDK redistributable next to ${target}"
    VERBATIM
  )
endfunction()
