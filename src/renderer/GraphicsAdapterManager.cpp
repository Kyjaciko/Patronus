#include "GraphicsAdapterManager.h"

namespace patronus::hardware
{

  GraphicsAdapter::GraphicsAdapter()
    : adapter_(nullptr)
    , description_{}
    , display_outputs_(kAverageDisplayCount)
  {
  }

  _Use_decl_annotations_
  GraphicsAdapter::GraphicsAdapter(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter)
    : GraphicsAdapter()
  {
    adapter_ = std::move(adapter);
    if (!adapter_) [[unlikely]]
      return;

    HRESULT hr;
    hr = adapter_->GetDesc3(&description_);
    if (FAILED(hr)) [[unlikely]]
      return;

    EnumerateDisplayOutputs(adapter_);
  }

  _Use_decl_annotations_
  void GraphicsAdapter::EnumerateDisplayOutputs(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter)
  {
    if (!adapter) [[unlikely]]
      return;

    HRESULT hr;
    for (UINT output_index = 0;; ++output_index)
    {
      Microsoft::WRL::ComPtr<IDXGIOutput> output;
      hr = adapter->EnumOutputs(output_index, &output);
      if (hr == DXGI_ERROR_NOT_FOUND)
        break;

      if (FAILED(hr)) [[unlikely]]
        continue;

      Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
      hr = output.As(&output6);
      if (FAILED(hr)) [[unlikely]]
        continue;

      display_outputs_.emplace_back(std::move(output6));
    }
  }

  _Use_decl_annotations_
  HRESULT GraphicsAdapter::GetMemoryInfo(MemoryInfo& result, const uint32_t node, const DXGI_MEMORY_SEGMENT_GROUP segment) const
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

  GraphicsAdapterManager::GraphicsAdapterManager()
    : warp_adapter_()
    , hardware_adapters_(kAverageAdapterCount)
  {
    EnumerateAdapters();
  }

  void GraphicsAdapterManager::EnumerateAdapters(const DXGI_GPU_PREFERENCE gpu_preference)
  {
    hardware_adapters_.clear();

    // TODO: should return why it failed.
    HRESULT hr;
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) [[unlikely]]
      return;

    for (UINT i = 0;; ++i)
    {
      Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
      hr = factory->EnumAdapterByGpuPreference(i, gpu_preference, IID_PPV_ARGS(&adapter));
      if (hr == DXGI_ERROR_NOT_FOUND)
        break;

      if (FAILED(hr)) [[unlikely]]
        return;

      hardware_adapters_.emplace_back(std::move(adapter));
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter4> warp_adapter;
    hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter));
    if (FAILED(hr)) [[unlikely]]
      return;

    warp_adapter_ = GraphicsAdapter(warp_adapter);
  }

}  // namespace patronus::hardware