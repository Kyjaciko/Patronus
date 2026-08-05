# Downloads the DirectX Agility SDK redistributable + headers.
#
# The Agility SDK is only published as a NuGet package (Microsoft.Direct3D.D3D12).
# A .nupkg is a plain zip file under a different extension, so we download it
# straight from nuget.org's package endpoint and extract it ourselves --
# no nuget.exe, no Visual Studio NuGet integration, nothing MSBuild-specific.
#
# Bump PATRONUS_AGILITY_SDK_VERSION to update; the URL below always resolves
# to that exact version's .nupkg.
set(PATRONUS_AGILITY_SDK_VERSION "1.619.5")

set(_agility_dir "${FETCHCONTENT_BASE_DIR}/agilitysdk-${PATRONUS_AGILITY_SDK_VERSION}")
set(_agility_zip "${_agility_dir}.nupkg.zip")

if(NOT EXISTS "${_agility_dir}/build/native/include/d3d12.h")
  file(DOWNLOAD
    "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/${PATRONUS_AGILITY_SDK_VERSION}"
    "${_agility_zip}"
    STATUS _download_status
  )
  list(GET _download_status 0 _download_code)
  if(NOT _download_code EQUAL 0)
    message(FATAL_ERROR "Failed to download Agility SDK ${PATRONUS_AGILITY_SDK_VERSION}: ${_download_status}")
  endif()

  file(ARCHIVE_EXTRACT INPUT "${_agility_zip}" DESTINATION "${_agility_dir}")
endif()

# Headers matching this exact Agility SDK build (preferred over the Windows
# SDK's own d3d12.h, which may be older than the redistributable we're
# shipping) and the redistributable DLLs we deploy next to the exe.
set(PATRONUS_AGILITY_SDK_INCLUDE_DIR "${_agility_dir}/build/native/include" CACHE PATH "" FORCE)
set(PATRONUS_AGILITY_SDK_BIN_DIR "${_agility_dir}/build/native/bin/x64" CACHE PATH "" FORCE)
