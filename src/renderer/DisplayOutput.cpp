#include "DisplayOutput.h"

namespace patronus::hardware
{

  DisplayMode::DisplayMode()
    : output_format_(renderer::settings::OutputFormat::kSdr)
    , description_{}
  {
  }

  DisplayMode::DisplayMode(renderer::settings::OutputFormat format, const DXGI_MODE_DESC1& description)
    : output_format_(format)
    , description_(description)
  {
  }

  DisplayOutput::DisplayOutput()
    : display_info_{}
    , output_(nullptr)
    , description_{}
    , display_modes_(kAverageModeCount)
    , primary_display_mode_index_(renderer::settings::kInvalidIndex)
  {
  }

  _Use_decl_annotations_
  DisplayOutput::DisplayOutput(Microsoft::WRL::ComPtr<IDXGIOutput6> output)
    : DisplayOutput()
  {
    output_ = std::move(output);
    if (!output_) [[unlikely]]
      return;
      
    HRESULT hr;
    hr = output_->GetDesc1(&description_);
    if (FAILED(hr)) [[unlikely]]
      return;
    
    EnumerateDisplayModes();
  }

  _Use_decl_annotations_
  void DisplayOutput::EnumerateDisplayModes(const UINT enumeration_mode)
  {
    if (!output_) [[unlikely]]
      return;

    HRESULT hr;
    for (uint8_t i = 0; i < static_cast<uint8_t>(renderer::settings::OutputFormat::kCount); ++i)
    {
      UINT mode_count = 0;
      const renderer::settings::OutputFormat output_format = static_cast<renderer::settings::OutputFormat>(i);
      const DXGI_FORMAT format = renderer::settings::GetOutputFormatDescription(output_format).swap_chain_format;

      // Get the number of display modes.
      hr = output_->GetDisplayModeList1(format, enumeration_mode, &mode_count, nullptr);
      if (FAILED(hr) || !mode_count)
        continue;

      // Get the display modes.
      std::vector<DXGI_MODE_DESC1> modes(mode_count);
      hr = output_->GetDisplayModeList1(format, enumeration_mode, &mode_count, modes.data());
      if (FAILED(hr)) [[unlikely]]
        continue;

      if (i == static_cast<uint8_t>(renderer::settings::OutputFormat::kHdr10))
        display_info_.hdr_available = true;

      for (const auto& mode: modes)
      {
        display_modes_.emplace_back(output_format, mode);
      }
    }
  }

  _Use_decl_annotations_
  const DisplayMode& DisplayOutput::GetCurrentDisplayMode(renderer::settings::OutputFormat standard_format, DXGI_MODE_SCANLINE_ORDER scanline_order, const DWORD info_type)
  {
    if (primary_display_mode_index_ != renderer::settings::kInvalidIndex)
      return display_modes_[primary_display_mode_index_];
    
    DEVMODEW dev_mode{
      .dmSize = sizeof(dev_mode)
    };

    // TODO: legal since the vector is filled through initialization but still not a good practice. 
    if (!EnumDisplaySettingsW(description_.DeviceName, info_type, &dev_mode))
      return display_modes_[0];

    DXGI_MODE_DESC1 mode_to_match{
      .Width            = dev_mode.dmPelsWidth,
      .Height           = dev_mode.dmPelsHeight,
      .RefreshRate      = { .Numerator = dev_mode.dmDisplayFrequency, .Denominator = 1 },
      .Format           = renderer::settings::GetOutputFormatDescription(standard_format).swap_chain_format,
      .ScanlineOrdering = scanline_order
    };

    DXGI_MODE_DESC1 closest_match{};
    HRESULT hr = output_->FindClosestMatchingMode1(&mode_to_match, &closest_match, nullptr);
    if (FAILED(hr))
      return display_modes_[0];

    for (std::ptrdiff_t i = 0; i < std::ssize(display_modes_); ++i)
    {
      const DisplayMode& mode = display_modes_[i];
      if (mode.GetOutputFormat() != standard_format)
        continue;

      const DXGI_MODE_DESC1 mode_description = mode.GetDescription();
      if (closest_match.Width != mode_description.Width || closest_match.Height != mode_description.Height
        || closest_match.RefreshRate.Numerator != mode_description.RefreshRate.Numerator || closest_match.RefreshRate.Denominator != mode_description.RefreshRate.Denominator)
        continue;

      primary_display_mode_index_ = i;
      return mode;
    }

    return display_modes_[0];
  }

}  // namespace patronus::hardware