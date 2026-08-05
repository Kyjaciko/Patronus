# Downloads DXC (DirectXShaderCompiler), for later HLSL SM 6.6 shader builds.
#
# NOTE: DXC does NOT ship inside the Agility SDK NuGet package
# (Microsoft.Direct3D.D3D12) -- that package only contains the D3D12 runtime
# redistributable. DXC is its own package, Microsoft.Direct3D.DXC. Same
# fetch mechanism as the Agility SDK: download the .nupkg as a zip, extract.
set(PATRONUS_DXC_VERSION "1.9.2607.13")

set(_dxc_dir "${FETCHCONTENT_BASE_DIR}/dxc-${PATRONUS_DXC_VERSION}")
set(_dxc_zip "${_dxc_dir}.nupkg.zip")

if(NOT EXISTS "${_dxc_dir}/build/native/bin/x64/dxc.exe")
  file(DOWNLOAD
    "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.DXC/${PATRONUS_DXC_VERSION}"
    "${_dxc_zip}"
    STATUS _download_status
  )
  list(GET _download_status 0 _download_code)
  if(NOT _download_code EQUAL 0)
    message(FATAL_ERROR "Failed to download DXC ${PATRONUS_DXC_VERSION}: ${_download_status}")
  endif()

  file(ARCHIVE_EXTRACT INPUT "${_dxc_zip}" DESTINATION "${_dxc_dir}")
endif()

set(PATRONUS_DXC_EXECUTABLE "${_dxc_dir}/build/native/bin/x64/dxc.exe" CACHE FILEPATH "" FORCE)
set(PATRONUS_DXC_BIN_DIR "${_dxc_dir}/build/native/bin/x64" CACHE PATH "" FORCE)
