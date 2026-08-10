#pragma once

#include "DXSample.h"
#include "COMException.h"

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

  void OnInit() override;
  void OnUpdate() override;
  void OnRender() override;
  void OnDestroy() override;

private:
  //static constexpr UINT FrameCount = 2;
  static constexpr UINT kBufferCount = 3;
  static constexpr UINT kFramesInFlight = 2;

  struct Vertex
  {
    XMFLOAT3 position;
    XMFLOAT4 color;
  };

  // Pipeline objects.
  CD3DX12_VIEWPORT m_viewport;
  CD3DX12_RECT m_scissorRect;
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

  // Synchronization objects.
  UINT m_frameIndex; // per-frame resources;  Sequence: 0..kFramesInFlight-1 -> 0,1,0,1,0,1,...
  UINT m_backBufferIndex; // RTVs;            Sequence: 0..kBufferCount-1 -> 0,1,2,0,1,2,...
  UINT64 m_nextFenceValue;
  HANDLE m_fenceEvent;
  HANDLE m_frameLatencyWaitable;
  ComPtr<ID3D12Fence> m_fence;
  UINT64 m_fenceValues[kFramesInFlight]; //   Sequence: { 2, 3 }, { 4, 5 }, { 6, 7 }, ...

  void LoadPipeline();
  void LoadAssets();
  void PopulateCommandList();
  //void WaitForPreviousFrame();
  void WaitForGpu();
  void BeginFrame();
  void EndFrame();
};