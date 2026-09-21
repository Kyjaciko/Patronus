#include "GraphicsAdapter.h"

namespace patronus::hardware
{

  GraphicsAdapter::GraphicsAdapter()
    : adapter_(nullptr)
    , description_{}
  {
  }

  GraphicsAdapter::GraphicsAdapter(Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter)
    : adapter_(std::move(adapter))
  {
    if (!adapter_) [[unlikely]]
      return;

    adapter_->GetDesc3(&description_);
  }

  GraphicsAdapter::~GraphicsAdapter()
  {
    adapter_.Reset();
  }

  _Use_decl_annotations_
  HRESULT GraphicsAdapter::GetMemoryInfo(MemoryInfo& result, const uint32_t node, const DXGI_MEMORY_SEGMENT_GROUP segment)
  {
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    const HRESULT hr = adapter_->QueryVideoMemoryInfo(node, segment, &info);
    if (FAILED(hr)) [[unlikely]]
      return hr;

    const uint64_t budget = static_cast<uint64_t>(info.Budget);
    const uint64_t usage = static_cast<uint64_t>(info.CurrentUsage);

    result = {
      .budget = budget,
      .usage = usage,
      .available = budget > usage ? budget - usage : 0ull,
      .reservation = static_cast<uint64_t>(info.CurrentReservation)
    };

    return hr;
  }

  GraphicsAdapters::GraphicsAdapters()
    : hardware_adapters_(kAverageAdapterCount)
  {
    EnumerateAdapters();
  }

  void GraphicsAdapters::EnumerateAdapters(const DXGI_GPU_PREFERENCE gpu_preference)
  {
    hardware_adapters_.clear();

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory6;
    COM_ERROR_IF_FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory6)), "Failed to create a DXGI factory.");

    HRESULT hr;
    for (UINT i = 0;; ++i)
    {
      Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
      hr = factory->EnumAdapterByGpuPreference(i, gpu_preference, IID_PPV_ARGS(&adapter3));
      if (hr == DXGI_ERROR_NOT_FOUND)
        break;

      hardware_adapters_.emplace_back(std::move(adapter3));
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter3> warp_adapter3;
    hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter3));
    if (FAILED(hr)) [[unlikely]]
      return;

    warp_adapter_ = GraphicsAdapter(warp_adapter3);
  }

}  // namespace patronus::hardware