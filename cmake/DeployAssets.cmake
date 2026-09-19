# Copies the repo's assets/ tree next to a target's executable after every
# build, as <exe dir>/assets/. Baked textures (curl-noise volumes, curve
# atlases, 2D noise) live in assets/ in source control and the app loads
# them through GetAssetFullPath(L"assets/<name>"), so the executable never
# depends on the current working directory. (Loading by a path relative to
# the CWD works from build_and_run.bat and breaks from the debugger or PIX.)
#
# copy_directory_if_different needs CMake 3.26; the project minimum is 3.28.
function(patronus_deploy_assets target)
  set(_assets_dir "${CMAKE_SOURCE_DIR}/assets")
  if(NOT IS_DIRECTORY "${_assets_dir}")
    message(FATAL_ERROR "patronus_deploy_assets(${target}): ${_assets_dir} is not a directory")
  endif()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${_assets_dir}" "$<TARGET_FILE_DIR:${target}>/assets"
    COMMENT "Deploying assets/ next to ${target}"
    VERBATIM
  )
endfunction()
