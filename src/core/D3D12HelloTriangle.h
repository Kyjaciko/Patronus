#pragma once

#include <vector>
#include <fstream>
#include <random>

#include "DXSample.h"
#include "COMException.h"

#include "HPTimer.h"
#include "Camera3D.h"
#include "Utils.h"
#include "profiling/FrameTimingWriter.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12HelloTriangle : public DXSample
{
public:
  D3D12HelloTriangle(UINT width, UINT height, std::wstring name);
  ~D3D12HelloTriangle();

protected:
  void OnInit() override;
  void OnUpdate() override;
  void OnRender() override;
  void OnSizeChanged(UINT width, UINT height, bool minimized) override;
  void OnDestroy() override;

private:
  // Two independent numbers, not shared:
  //
  // kBufferCount    — swapchain surfaces. One is on screen, one is queued for
  //                   the next flip, one is free to render into. Three keeps
  //                   Present from blocking when a frame runs long.
  // kFramesInFlight — how far the CPU may run ahead of the GPU.
  //
  static constexpr UINT kBufferCount = 3;
  static constexpr UINT kFramesInFlight = 2;

  static constexpr UINT kParticleCount = 1'000'000;

  static constexpr UINT kTexturePixelSize = 8; // DXGI_FORMAT_R16G16B16A16_FLOAT = 8 bytes

  static constexpr float kMaxDeltaTime = .1f;

  static constexpr UINT kZonesPerFrame = 3; // Zone layout within 1 frame:
                                            // 0 = frame         1 = sim         2 = render
  static constexpr UINT kSlotsPerFrame = 6; // Slot layout within 1 frame:
                                            // 0 = frame begin   1 = sim begin   2 = sim end
                                            // 3 = render begin  4 = render end  5 = frame end

  struct Vertex
  {
    XMFLOAT3 position;
    XMFLOAT4 color;
  };

  // Pipeline objects.
  CD3DX12_VIEWPORT m_viewport;
  CD3DX12_RECT m_scissorRect;
  ComPtr<IDXGIAdapter1> m_hardwareAdapter;
  ComPtr<IDXGISwapChain3> m_swapChain;
  ComPtr<ID3D12Device> m_device;
  ComPtr<ID3D12Resource> m_renderTargets[kBufferCount];
  ComPtr<ID3D12CommandAllocator> m_commandAllocators[kFramesInFlight];
  ComPtr<ID3D12CommandQueue> m_commandQueue;
  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
  ComPtr<ID3D12PipelineState> m_pipelineState;
  ComPtr<ID3D12GraphicsCommandList> m_commandList;
  UINT m_rtvDescriptorSize;

  // App resources.
  ComPtr<ID3D12Resource> m_vertexBuffer;
  D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

  ComPtr<ID3D12Resource> m_indexBuffer;
  ComPtr<ID3D12Resource> m_indexUploadBuffer;
  D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

  // Synchronization objects.
  UINT m_frameIndex; // per-frame resources;  Sequence: 0..kFramesInFlight-1 -> 0,1,0,1,0,1,...
  UINT m_backBufferIndex; // RTVs;            Sequence: 0..kBufferCount-1 -> 0,1,2,0,1,2,...
  UINT64 m_nextFenceValue;
  HANDLE m_fenceEvent;
  HANDLE m_frameLatencyWaitable;
  ComPtr<ID3D12Fence> m_fence;
  UINT64 m_fenceValues[kFramesInFlight];

  // Particle system.
  ComPtr<ID3D12DescriptorHeap> m_particleSrvUavHeap;
  UINT m_particleSrvUavDescriptorSize;

  enum ParticleHeap : UINT 
  {
    PoolUAV = 0, // Compute shader reads and updates the particles in this buffer.
    CurlNoiseSRV, // Must immediately follow PoolUAV because the compute root signature (see srvUavRange) expects u0 and t0 to be contiguous in the descriptor table.
    PoolSRV, // Vertex shader reads the particle data from this buffer.
    DearImGui, // Dear ImGui font texture.
    Count
  };

  ComPtr<ID3D12PipelineState> m_particlePipelineState;
  ComPtr<ID3D12RootSignature> m_computeRootSignature;
  ComPtr<ID3D12PipelineState> m_computePipelineState;
  ComPtr<ID3D12Resource> m_particleUploadBuffer; // Since Default Heap can't directly be written to from the CPU we use a upload heap.
  ComPtr<ID3D12Resource> m_particlePool; // Pre-allocated structured buffer, to 'remove' dynamic memory allocations in GPU memory.
  ComPtr<ID3D12Resource> m_cameraCB[kFramesInFlight];
  Camera3D m_camera;

  struct Particle
  {
    DirectX::XMFLOAT3 pos{};
    DirectX::XMFLOAT3 vel{};
    float             lifetime{};
    float             _pad{};
  };

  // Variables within constant buffers need to be 16 byte aligned!
  struct CameraCB
  {
    // 4x 16 bytes.
    DirectX::XMFLOAT4X4 viewProj{};

    // 16 bytes.
    DirectX::XMFLOAT3 camRight{};
    float             billboardSize{};

    // 16 bytes.
    DirectX::XMFLOAT3 camUp{};
    float             _pad{};
  };

  // Check if 16 byte alignment is met.
  static_assert(sizeof(CameraCB) % 16 == 0);

  struct SimulationConstants
  {
    float deltaTime{};
    UINT particleCount{};
  };

  HPTimer m_timer;
  SimulationConstants m_particleSimConstants{.particleCount = kParticleCount};

  ComPtr<ID3D12Resource> m_rawCurlNoiseDataHeap;
  ComPtr<ID3D12Resource> m_curlNoiseTextureHeap;

  // Timestamp queries for measuring GPU time.
  ComPtr<ID3D12QueryHeap> m_timestampQueryHeap;
  ComPtr<ID3D12Resource> m_timestampQueryResult;
  UINT64 m_timestampFrequency;
  UINT64 m_frameNumber;
  patronus::profiling::FrameTimingWriter m_timestampWriter;

  enum TimestampSlots : UINT
  {
    FRAME_BEGIN = 0,
    SIM_BEGIN,
    SIM_END,
    RENDER_BEGIN,
    RENDER_END,
    FRAME_END
  };

  void UpdateCameraCB(const ComPtr<ID3D12Resource>& camera_constant_buffer);
  void ReadTimestamps();

  // Window state.
  bool m_windowVisible;
  bool m_windowedMode;

  void LoadPipeline();
  void LoadAssets();
  void PopulateCommandList();
  void WaitForGpu();
  void BeginFrame();
  void EndFrame();
};