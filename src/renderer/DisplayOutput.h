#pragma once

#include "core/stdafx.h"
#include "renderer/Settings.h"

namespace patronus::hardware
{

  class DisplayMode
  {
  public:
    struct Resolution
    {
      uint32_t width{};
      uint32_t height{};
    };

    struct RefreshRate
    {
      uint32_t numerator{};
      uint32_t denominator{};
    };

    DisplayMode();
    DisplayMode(renderer::settings::OutputFormat format, const DXGI_MODE_DESC1& description);

    const DXGI_MODE_DESC1& GetDescription() const noexcept                           { return description_; }
    renderer::settings::OutputFormat GetOutputFormat() const noexcept                { return output_format_; }
    renderer::settings::OutputFormatDesc GetOutputFormatDescription() const noexcept { return renderer::settings::GetOutputFormatDescription(output_format_); }
    
    Resolution GetResolution() const noexcept                                        { return { .width = description_.Width, .height = description_.Height }; }
    RefreshRate GetRefreshRate() const noexcept                                      { return { .numerator = description_.RefreshRate.Numerator, .denominator = description_.RefreshRate.Denominator }; }
    std::string_view GetOutputFormatName() const noexcept                            { return renderer::settings::GetOutputFormatDescription(output_format_).name; }

  private:
    renderer::settings::OutputFormat output_format_;
    DXGI_MODE_DESC1                  description_;
  };

  class DisplayOutput
  {
    static constexpr std::ptrdiff_t kAverageModeCount{3};

  public:
    struct DisplayInfo
    {
      bool hdr_available{};
    };

    DisplayOutput();
    explicit DisplayOutput(_In_ Microsoft::WRL::ComPtr<IDXGIOutput6> output);

    void EnumerateDisplayModes(_In_ Microsoft::WRL::ComPtr<IDXGIOutput6> output, const UINT enumeration_mode = DXGI_ENUM_MODES_INTERLACED); 

    IDXGIOutput6* GetNativeOutput() const noexcept           { return output_.Get(); }
    const DXGI_OUTPUT_DESC1& GetDescription() const noexcept { return description_; }

    const wchar_t* GetName() const noexcept                  { return description_.DeviceName; }
    uint32_t GetBitsPerColor() const noexcept                { return description_.BitsPerColor;}
    std::array<float, 2> GetRedPrimary() const noexcept      { return { description_.RedPrimary[0], description_.RedPrimary[1] }; }
    std::array<float, 2> GetGreenPrimary() const noexcept    { return { description_.GreenPrimary[0], description_.GreenPrimary[1] }; }
    std::array<float, 2> GetBluePrimary() const noexcept     { return { description_.BluePrimary[0], description_.BluePrimary[1] }; }
    std::array<float, 2> GetWhitePoint() const noexcept      { return { description_.WhitePoint[0], description_.WhitePoint[1] }; }
    float GetMinLuminance() const noexcept                   { return description_.MinLuminance; }
    float GetMaxLuminance() const noexcept                   { return description_.MaxLuminance; }
    float GetMaxFullFrameLuminance() const noexcept          { return description_.MaxFullFrameLuminance; }
    
    bool IsHdrSupported() const noexcept                     { return display_info_.hdr_available; }

  private:
    DisplayInfo                          display_info_;
    Microsoft::WRL::ComPtr<IDXGIOutput6> output_;
    DXGI_OUTPUT_DESC1                    description_;
    std::vector<DisplayMode>             display_modes_;
  };

}  // namespace patronus::hardware