#include "stdafx.h"
#include "DXSample.h"

using namespace Microsoft::WRL;

DXSample::DXSample(UINT width, UINT height, std::wstring name) :
  m_width(width),
  m_height(height),
  m_title(name),
  m_useWarpDevice(false),
  m_mouse(std::make_unique<Mouse>()),
  m_keyboard(std::make_unique<Keyboard>())
{
  WCHAR assetsPath[512];
  GetAssetsPath(assetsPath, _countof(assetsPath));
  m_assetsPath = assetsPath;

  m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);

  RAWINPUTDEVICE raw_input_device{
    .usUsagePage = 0x01,
    .usUsage     = 0x02,    // Mouse.
    .dwFlags	   = 0,
    .hwndTarget  = nullptr  // No target window, so it follows keyboard focus.
  };

  if (RegisterRawInputDevices(&raw_input_device, 1, sizeof(raw_input_device)) == FALSE) 
  {
    COM_ERROR_IF_FAILED(E_FAIL, "Failed to register mouse as a raw input device.");
  }
}

DXSample::~DXSample()
{
}

// Helper function for resolving the full path of assets.
std::wstring DXSample::GetAssetFullPath(LPCWSTR assetName)
{
  return m_assetsPath + assetName;
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_
void DXSample::GetHardwareAdapter(
  IDXGIFactory1* pFactory,
  IDXGIAdapter1** ppAdapter,
  bool requestHighPerformanceAdapter)
{
  *ppAdapter = nullptr;

  ComPtr<IDXGIAdapter1> adapter;

  ComPtr<IDXGIFactory6> factory6;
  if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
  {
    for (
      UINT adapterIndex = 0;
      SUCCEEDED(factory6->EnumAdapterByGpuPreference(
        adapterIndex,
        requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
        IID_PPV_ARGS(&adapter)));
      ++adapterIndex)
    {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      {
        // Don't select the Basic Render Driver adapter.
        // If you want a software adapter, pass in "/warp" on the command line.
        continue;
      }

      // Check to see whether the adapter supports Direct3D 12, but don't create the
      // actual device yet.
      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
      {
        break;
      }
    }
  }

  if(adapter.Get() == nullptr)
  {
    for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
    {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      {
        // Don't select the Basic Render Driver adapter.
        // If you want a software adapter, pass in "/warp" on the command line.
        continue;
      }

      // Check to see whether the adapter supports Direct3D 12, but don't create the
      // actual device yet.
      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
      {
        break;
      }
    }
  }
  
  *ppAdapter = adapter.Detach();
}

// Helper function for setting the window's title text.
void DXSample::SetCustomWindowText(LPCWSTR text)
{
  std::wstring windowText = m_title + L": " + text;
  SetWindowText(Win32Application::GetHwnd(), windowText.c_str());
}

// Helper function for parsing any supplied command line args.
_Use_decl_annotations_
void DXSample::ParseCommandLineArgs(WCHAR* argv[], int argc)
{
  for (int i = 1; i < argc; ++i)
  {
    if (_wcsnicmp(argv[i], L"-warp", wcslen(argv[i])) == 0 || 
      _wcsnicmp(argv[i], L"/warp", wcslen(argv[i])) == 0)
    {
      m_useWarpDevice = true;
      m_title = m_title + L" (WARP)";
    }
  }
}