#pragma once

#include "core/stdafx.h"
#include "renderer/DisplayOutput.h"

namespace patronus::hardware
{

  class GraphicsAdapter
  {
    static constexpr std::ptrdiff_t kAverageDisplayCount{2};

  public:
    struct MemoryInfo
    {
      uint64_t budget{};
      uint64_t usage{};
      uint64_t available{};
      uint64_t reservation{};

      bool IsEmpty() const noexcept { return (budget == 0 && usage == 0 && available == 0 && reservation == 0); }
    };

    GraphicsAdapter();
    explicit GraphicsAdapter(_In_ Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter);

    void EnumerateDisplayOutputs(_In_ Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter); 
    HRESULT GetMemoryInfo(_Out_ MemoryInfo& result, const uint32_t node = 0, const DXGI_MEMORY_SEGMENT_GROUP segment = DXGI_MEMORY_SEGMENT_GROUP_LOCAL) const;

    IDXGIAdapter4* GetNativeAdapter() const noexcept          { return adapter_.Get(); }
    const DXGI_ADAPTER_DESC3& GetDescription() const noexcept { return description_; }
    
    const wchar_t* GetName() const noexcept                   { return description_.Description; }
    uint32_t GetVendorId() const noexcept                     { return description_.VendorId; }
    uint32_t GetDeviceId() const noexcept                     { return description_.DeviceId; }
    std::ptrdiff_t GetDedicatedVideoMemory() const noexcept   { return description_.DedicatedVideoMemory; }
    std::ptrdiff_t GetDedicatedSytemMemory() const noexcept   { return description_.DedicatedSystemMemory; }
    std::ptrdiff_t GetSharedSytemMemory() const noexcept      { return description_.SharedSystemMemory; }

    const std::vector<DisplayOutput>& GetDisplayOutputs() const noexcept { return display_outputs_; }

  private:
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
    DXGI_ADAPTER_DESC3                    description_;
    std::vector<DisplayOutput>            display_outputs_;
  };

  class GraphicsAdapterManager
  {
    static constexpr std::ptrdiff_t kAverageAdapterCount{2};

  public:
    GraphicsAdapterManager();

    void EnumerateAdapters(const DXGI_GPU_PREFERENCE gpu_preference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE); 

    const GraphicsAdapter& GetWarpAdapter() const noexcept                   { return warp_adapter_; }
    const std::vector<GraphicsAdapter>& GetHardwareAdapters() const noexcept { return hardware_adapters_; }
    std::ptrdiff_t GetHardwareAdapterCount() const noexcept                  { return hardware_adapters_.size(); }

  private:
    GraphicsAdapter              warp_adapter_;
    std::vector<GraphicsAdapter> hardware_adapters_; // From high to low performance according to DXGI's GPU preference.
  };

}  // namespace patronus::hardware