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
    
    EnumerateDisplayModes(output_);
  }

  _Use_decl_annotations_
  void DisplayOutput::EnumerateDisplayModes(Microsoft::WRL::ComPtr<IDXGIOutput6> output, const UINT enumeration_mode)
  {
    if (!output) [[unlikely]]
      return;

    HRESULT hr;
    for (uint8_t i = 0; i < static_cast<uint8_t>(renderer::settings::OutputFormat::kCount); ++i)
    {
      UINT mode_count = 0;
      const renderer::settings::OutputFormat output_format = static_cast<renderer::settings::OutputFormat>(i);
      const DXGI_FORMAT format = renderer::settings::GetOutputFormatDescription(output_format).swap_chain_format;

      // Get the number of display modes.
      hr = output->GetDisplayModeList1(format, enumeration_mode, &mode_count, nullptr);
      if (FAILED(hr) || !mode_count)
        continue;

      // Get the display modes.
      std::vector<DXGI_MODE_DESC1> modes(mode_count);
      hr = output->GetDisplayModeList1(format, enumeration_mode, &mode_count, modes.data());
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

}  // namespace patronus::hardware