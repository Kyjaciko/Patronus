// Build-system smoke test. This is an explicitly authorized exception to
// the CLAUDE.md hard rule against writing RHI code (see CLAUDE.md and
// docs/adr/0001-tech-stack.md) -- narrow in scope on purpose:
//
//   - Create a D3D12 device (no window, no swapchain, no render loop).
//   - Prove the app is actually running against the deployed Agility SDK
//     redistributable, not whatever core ships with the OS.
//   - Confirm the debug layer and GPU-Based Validation came up.
//   - Exit.
//
// Its only job is to prove the build system, dependency fetch, and Agility
// SDK deployment all work end to end. It is not a foundation for the real
// renderer -- that gets written (by the user) from scratch in src/rhi/.

#include <d3d12.h>
#include <dxgi1_6.h>
#include <winver.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

using Microsoft::WRL::ComPtr;

// Defined in AgilitySDKExports.cpp; declared extern here so this
// translation unit can print the version it was compiled against.
extern "C" extern const UINT D3D12SDKVersion;

namespace {

// Reports where D3D12Core.dll actually loaded from and its file version.
// If Agility SDK deployment (see CMakeLists.txt) worked, the path below
// should point at .../D3D12/D3D12Core.dll next to this exe, not System32.
void PrintAgilitySdkModuleInfo() {
  const HMODULE core = GetModuleHandleW(L"D3D12Core.dll");
  if (core == nullptr) {
    wprintf(L"  D3D12Core.dll: not loaded as a separate module (in-box core in use)\n");
    return;
  }

  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(core, path, MAX_PATH);
  wprintf(L"  D3D12Core.dll loaded from: %s\n", path);

  const DWORD size = GetFileVersionInfoSizeW(path, nullptr);
  if (size == 0) {
    return;
  }
  std::vector<BYTE> buffer(size);
  if (!GetFileVersionInfoW(path, 0, size, buffer.data())) {
    return;
  }

  VS_FIXEDFILEINFO* info = nullptr;
  UINT infoSize = 0;
  if (VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) && info != nullptr) {
    wprintf(L"  D3D12Core.dll file version: %u.%u.%u.%u\n", HIWORD(info->dwFileVersionMS),
            LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
  }
}

// Prefers a real hardware adapter; falls back to the WARP software adapter
// so this still runs as a CI build-verification step on GPU-less runners.
ComPtr<IDXGIAdapter1> PickAdapter(IDXGIFactory6* factory, bool& out_is_warp) {
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                       IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
       ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }
    // Passing nullptr as the device output just probes support for this
    // feature level without creating a device.
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
      out_is_warp = false;
      return adapter;
    }
  }

  if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) {
    out_is_warp = true;
    return adapter;
  }

  out_is_warp = false;
  return nullptr;
}

}  // namespace

int main() {
  wprintf(L"Patronus smoke test -- build system + Agility SDK verification only.\n\n");

  // The debug layer must be enabled before device creation to have any
  // effect on that device.
  ComPtr<ID3D12Debug> debug;
  ComPtr<ID3D12Debug1> debug1;
  bool gpu_based_validation_requested = false;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
    debug->EnableDebugLayer();
    if (SUCCEEDED(debug.As(&debug1))) {
      debug1->SetEnableGPUBasedValidation(TRUE);
      gpu_based_validation_requested = true;
    }
  } else {
    wprintf(
        L"WARNING: D3D12 debug layer unavailable "
        L"(is the 'Graphics Tools' optional Windows feature installed?)\n");
  }

  ComPtr<IDXGIFactory6> factory;
  if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)))) {
    wprintf(L"FATAL: CreateDXGIFactory2 failed.\n");
    return EXIT_FAILURE;
  }

  bool is_warp = false;
  const ComPtr<IDXGIAdapter1> adapter = PickAdapter(factory.Get(), is_warp);
  if (adapter == nullptr) {
    wprintf(L"FATAL: no usable D3D12 adapter found (not even WARP).\n");
    return EXIT_FAILURE;
  }

  DXGI_ADAPTER_DESC1 desc{};
  adapter->GetDesc1(&desc);
  wprintf(L"Adapter: %s%s\n", desc.Description, is_warp ? L" (WARP software adapter)" : L"");

  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    wprintf(L"FATAL: D3D12CreateDevice failed.\n");
    return EXIT_FAILURE;
  }
  wprintf(L"D3D12 device created successfully.\n\n");

  wprintf(L"Agility SDK:\n");
  wprintf(L"  D3D12SDKVersion export (compiled in): %u\n", D3D12SDKVersion);
  PrintAgilitySdkModuleInfo();
  wprintf(L"\n");

  wprintf(L"Debug layer:\n");
  ComPtr<ID3D12InfoQueue> info_queue;
  // ID3D12InfoQueue is only obtainable when the debug layer is active on
  // this device -- its presence is the confirmation, not a side effect.
  const bool debug_layer_active = SUCCEEDED(device.As(&info_queue));
  wprintf(L"  Debug layer active (ID3D12InfoQueue available): %s\n", debug_layer_active ? L"yes" : L"no");
  wprintf(L"  GPU-Based Validation requested before device creation: %s\n",
          gpu_based_validation_requested ? L"yes" : L"no");

  wprintf(L"\nSmoke test passed.\n");
  return EXIT_SUCCESS;
}
