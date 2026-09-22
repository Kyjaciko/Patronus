#pragma once

#include "core/stdafx.h"

namespace patronus
{

namespace renderer::settings
{

  enum class OutputFormat : uint8_t
  {
    kSdr = 0,
    kHdr10,
    kScRGB,
    kCount
  };

  struct OutputFormatDesc 
  {
    DXGI_FORMAT           swap_chain_format;
    DXGI_FORMAT           rtv_format;
    DXGI_COLOR_SPACE_TYPE color_space;
    std::string_view      name;
  };

  inline static constexpr OutputFormatDesc kOutputFormatsDesc[] = {
    { DXGI_FORMAT_R8G8B8A8_UNORM,     DXGI_FORMAT_R8G8B8A8_UNORM,     DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,    "SDR"},
    { DXGI_FORMAT_R10G10B10A2_UNORM,  DXGI_FORMAT_R10G10B10A2_UNORM,  DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, "HDR"},
    { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,    "scRGB"}
  };
  static_assert(std::size(kOutputFormatsDesc) == static_cast<size_t>(OutputFormat::kCount));

  inline static constexpr const OutputFormatDesc& GetOutputFormatDescription(OutputFormat format) noexcept
  {
    return kOutputFormatsDesc[static_cast<uint8_t>(format)];
  }

}  // namespace renderer::settings

}  // namespace patronus