#include "stdafx.h"
#include "D3D12HelloTriangle.h"

D3D12HelloTriangle::D3D12HelloTriangle(UINT width, UINT height, std::wstring name) :
  DXSample(width, height, name),
  m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
  m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
  m_rtvDescriptorSize(0),
  m_frameIndex(0),
  m_backBufferIndex(0),
  m_nextFenceValue(0),
  m_fenceEvent(nullptr),
  m_frameLatencyWaitable(nullptr),
  m_fenceValues{},
  m_windowVisible(true),
  m_windowedMode(true)
{
}

void D3D12HelloTriangle::OnInit()
{
  LoadPipeline();
  LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12HelloTriangle::LoadPipeline()
{
  UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
  // Enable the debug layer (requires the Graphics Tools "optional feature").
  // NOTE: Enabling the debug layer after device creation will invalidate the active device.
  {
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
      debugController->EnableDebugLayer();

      // Enable additional debug layers.
      dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }
#endif

  ComPtr<IDXGIFactory5> factory;
  COM_ERROR_IF_FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)), "Failed to create DXGI factory.");

  if (m_useWarpDevice)
  {
    ComPtr<IDXGIAdapter> warpAdapter;
    COM_ERROR_IF_FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)), "Failed to enumerate through the adapters.");

    COM_ERROR_IF_FAILED(D3D12CreateDevice(
        warpAdapter.Get(),
        D3D_FEATURE_LEVEL_12_2,
        IID_PPV_ARGS(&m_device)
      ), 
      "Failed to create the device."
    );
  }
  else
  {
    ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);

    COM_ERROR_IF_FAILED(D3D12CreateDevice(
        hardwareAdapter.Get(),
        D3D_FEATURE_LEVEL_12_2,
        IID_PPV_ARGS(&m_device)
      ), 
      "Failed to create the device."
    );
  }

#if defined(_DEBUG)
  ComPtr<ID3D12InfoQueue> infoQueue;
  if (SUCCEEDED(m_device.As(&infoQueue)))
  {
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
  }
#endif

  // Describe and create the command queue.
  D3D12_COMMAND_QUEUE_DESC queueDesc {
    .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
  };

  COM_ERROR_IF_FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "Failed to create the command queue.");

  COM_ERROR_IF_FAILED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &m_tearingSupport, sizeof(m_tearingSupport)), "Failed to check for hardware feature support.");

  // Describe and create the swap chain.
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc {
    .Width = m_width, 
    .Height = m_height, 
    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    .Stereo = FALSE,
    .SampleDesc = { .Count = 1, .Quality = 0 }, // MSAA turned OFF; flip models don't support this!
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = kBufferCount,
    .Scaling = DXGI_SCALING_NONE, // Disabled streching to test if window resizing works properly.
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .AlphaMode = DXGI_ALPHA_MODE_IGNORE, // OS Window ignores alpha channel (not the pipeline!)
    .Flags = 
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
      | (m_tearingSupport ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u) // Required for VRR (not yet used)
  };

  ComPtr<IDXGISwapChain1> swapChain;
  COM_ERROR_IF_FAILED(factory->CreateSwapChainForHwnd(
      m_commandQueue.Get(), // Swap chain needs the queue so that it can force a flush on it.
      Win32Application::GetHwnd(),
      &swapChainDesc,
      nullptr,
      nullptr,
      &swapChain
    ), 
    "Failed to create the swap chain."
  );

  // When tearing support is enabled our application will explicitly handle 
  // the fullscreen/windowed transitions instead of letting DXGI handle this automatically.
  if (m_tearingSupport) 
  {
    COM_ERROR_IF_FAILED(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER), "Failed to diasble DXGI automatic fullscreen handling.");
  }

  COM_ERROR_IF_FAILED(swapChain.As(&m_swapChain), "Failed to obtain the DXGI swap chain.");
  m_swapChain->SetMaximumFrameLatency(kFramesInFlight); // Set maximum number of Present() calls that will be queued.
  m_frameLatencyWaitable = m_swapChain->GetFrameLatencyWaitableObject();

  // Create descriptor heaps.
  {
    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = kBufferCount,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
    };

    COM_ERROR_IF_FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "Failed to create the descriptor heap.");

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(rtvHeapDesc.Type);
  }

  // Create frame resources.
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Create a RTV for each frame.
    for (UINT n = 0; n < kBufferCount; ++n)
    {
      COM_ERROR_IF_FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])), "Failed to obtain the swap chain back buffer.");
      m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
      rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
  }

  // Create command allocators.
  {
    // One allocator per frame in flight: while the GPU executes frame N's
    // commands, the CPU records frame N+1 into the other one.
    for (UINT n = 0; n < kFramesInFlight; ++n)
    {
      COM_ERROR_IF_FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])), "Failed to create a command allocator");
    }
  }
}

// Load the sample assets.
void D3D12HelloTriangle::LoadAssets()
{
  D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData {
    .HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1
  };

  if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
  {
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
  }

  // Create an empty root signature.
  {
    // Allow input layout and deny uneccessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(0, nullptr, 0, nullptr, rootSignatureFlags);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    COM_ERROR_IF_FAILED(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error), "Failed to serialize the root signature.");
    COM_ERROR_IF_FAILED(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "Failed to create the root signature.");
  }

  // Create the pipeline state, which includes compiling and loading shaders.
  {
    UINT8* pVertexShaderData = nullptr;
    UINT8* pPixelShaderData = nullptr;
    UINT vertexShaderDataLength = 0;
    UINT pixelShaderDataLength = 0;

    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(L"shaders_VSMain.cso").c_str(), &pVertexShaderData, &vertexShaderDataLength), "Failed to read the vertex shader.");
    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(L"shaders_PSMain.cso").c_str(), &pPixelShaderData, &pixelShaderDataLength), "Failed to read the pixel shader.");

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
      { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderData, vertexShaderDataLength);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderData, pixelShaderDataLength);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    COM_ERROR_IF_FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)), "Failed to create the graphics pipeline state.");
  
    free(pVertexShaderData);
    free(pPixelShaderData);
  }

  // Create the command list.
  COM_ERROR_IF_FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)), "Failed to create the command list.");

  // Command lists are created in the recording state, but there is nothing
  // to record yet. The main loop expects it to be closed, so close it now.
  COM_ERROR_IF_FAILED(m_commandList->Close(), "Failed to close the command list.");

  // Create the vertex buffer.
  {
    // Define the geometry for a triangle.
    Vertex triangleVertices[] =
    {
      { { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
      { { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
      { { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
    };

    const UINT vertexBufferSize = sizeof(triangleVertices);

    // Note: using upload heaps to transfer static data like vert buffers is not 
    // recommended. Every time the GPU needs it, the upload heap will be marshalled 
    // over. Please read up on Default Heap usage. An upload heap is used here for 
    // code simplicity and because there are very few verts to actually transfer.
    COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&m_vertexBuffer)), "Failed to create the vertex buffer.");

    // Copy the triangle data to the vertex buffer.
    UINT8* pVertexDataBegin;
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    COM_ERROR_IF_FAILED(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)), "Failed to map the vertex buffer.");
    memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
    m_vertexBuffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view.
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;
  }

  // Create synchronization objects and wait until assets have been uploaded to the GPU.
  {
    COM_ERROR_IF_FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "Failed to create fence");

    // Create an event handle to use for frame synchronization.
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
      COM_ERROR_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()), "Failed to create the fence event.");
    }

    WaitForGpu();
  }
}

// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
}

// Render the scene.
void D3D12HelloTriangle::OnRender()
{
  if (!m_windowVisible)
    return;

  BeginFrame();

  // Record all the commands we need to render the scene into the command list.
  PopulateCommandList();

  // Execute the command list.
  ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
  m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  // Present the frame.
  //COM_ERROR_IF_FAILED(m_swapChain->Present(1, 0), "Failed to present the frame.");

  // When using sync interval 0, it is recommended to always pass the tearing
  // flag when it is supported, even when presenting in windowed mode.
  // However, this flag cannot be used if the app is in fullscreen mode as a
  // result of calling SetFullscreenState.
  UINT present_flags = (m_tearingSupport && m_windowedMode) ? DXGI_PRESENT_ALLOW_TEARING : 0;
  COM_ERROR_IF_FAILED(m_swapChain->Present(0, present_flags), "Failed to present the frame.");

  EndFrame();
}

void D3D12HelloTriangle::OnSizeChanged(UINT width, UINT height, bool minimized)
{
  if (minimized || (width == m_width && height == m_height))
    goto UpdateWindowState;

  m_width = width;
  m_height = height;
  m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);

  // Flush all remaining GPU commands.
  WaitForGpu();

  // Release resources holding references to the swap chain.
  for (UINT n = 0; n < kBufferCount; ++n) 
  {
    m_renderTargets[n].Reset();
  }

  // Reset the fence values to the current fance value.
  for (UINT n = 0; n < kFramesInFlight; ++n) 
  {
    m_fenceValues[n] = m_fenceValues[m_frameIndex];
  }

  // Resize the swap chain.
  DXGI_SWAP_CHAIN_DESC swapChainDesc{};
  m_swapChain->GetDesc(&swapChainDesc);
  COM_ERROR_IF_FAILED(m_swapChain->ResizeBuffers(
      kBufferCount,
      m_width,
      m_height,
      swapChainDesc.BufferDesc.Format,
      swapChainDesc.Flags
    ), 
    "Failed to resize swap chain."
  );

  // Reset the frame index to the current back buffer index.
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

  BOOL fullscreenState;
  COM_ERROR_IF_FAILED(m_swapChain->GetFullscreenState(&fullscreenState, nullptr), "Failed to obtain fullscreen state.");
  m_windowedMode = !fullscreenState;

  // Recreate frame resources.
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Recreate a RTV for each frame.
    for (UINT n = 0; n < kBufferCount; ++n)
    {
      COM_ERROR_IF_FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])), "Failed to obtain the swap chain back buffer.");
      m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
      rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
  }

  // Resize screen viewport to match the current window size.
  m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height));
  m_scissorRect = CD3DX12_RECT(0.0f, 0.0f, static_cast<LONG>(m_width), static_cast<LONG>(m_height));

UpdateWindowState:
  m_windowVisible = !minimized;
}

void D3D12HelloTriangle::OnDestroy()
{
  // Ensure that the GPU is no longer referencing resources that are about to be
  // cleaned up by the destructor.
  WaitForGpu();

  if (!m_tearingSupport)
  {
    // Fullscreen state should always be false before exiting the app.
    COM_ERROR_IF_FAILED(m_swapChain->SetFullscreenState(FALSE, nullptr), "Failed to set fullscreen state to off.");
  }

  CloseHandle(m_fenceEvent);
  CloseHandle(m_frameLatencyWaitable);
}

void D3D12HelloTriangle::OnKeyDown(UINT8 key)
{
  switch (key)
  {

  // Instrument the Space Bar to toggle between fullscreen states.
  // The window message loop callback will receive a WM_SIZE message once the
  // window is in the fullscreen state. At that point, the IDXGISwapChain should
  // be resized to match the new window size
  case VK_SPACE:
  {
    if (m_tearingSupport)
    {
      Win32Application::ToggleFullscreenWindow(m_swapChain.Get());
    }
    else
    {
      BOOL fullscreen_state = FALSE;
      COM_ERROR_IF_FAILED(m_swapChain->GetFullscreenState(&fullscreen_state, nullptr), "Failed to obtain fullscreen state from the swap chain.");
      
      // Transitions to fullscreen mode can fail when running apps over
      // terminal services or for some other unexpected reason.
      COM_ERROR_IF_FAILED(m_swapChain->SetFullscreenState(!fullscreen_state, nullptr), "Fullscreen transition failed.");
    }
  }

  }
}

void D3D12HelloTriangle::PopulateCommandList()
{
  // Safe here because BeginFrame() waited on m_fenceValues[m_frameIndex].
  COM_ERROR_IF_FAILED(m_commandAllocators[m_frameIndex]->Reset(), "Failed to reset the command allocator.");
  COM_ERROR_IF_FAILED(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get()), "Failed to reset the command list.");

  // Set necessary state.
  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->RSSetViewports(1, &m_viewport);
  m_commandList->RSSetScissorRects(1, &m_scissorRect);

  // Indicate that the back buffer will be used as a render target.
  m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_backBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_backBufferIndex, m_rtvDescriptorSize);
  m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

  // Record commands.
  const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
  m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
  m_commandList->DrawInstanced(3, 1, 0, 0);

  // Indicate that the back buffer will now be used to present.
  m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_backBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

  COM_ERROR_IF_FAILED(m_commandList->Close(), "Failed to close the command list.");
}

void D3D12HelloTriangle::WaitForGpu()
{
  // Place a signal at the back of the queue to know everything is completed.
  const UINT64 value = ++m_nextFenceValue;
  COM_ERROR_IF_FAILED(m_commandQueue->Signal(m_fence.Get(), value), "Failed to signal command queue fence.");
  if (m_fence->GetCompletedValue() < value)
  {
    COM_ERROR_IF_FAILED(m_fence->SetEventOnCompletion(value, m_fenceEvent), "Failed to set fence completion event.");
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
  }
}

void D3D12HelloTriangle::BeginFrame()
{
  // Wait until a new frame can be queued (no more than kFramesInFlight amount of Present() calls can be in DXGI's present-queue).
  WaitForSingleObjectEx(m_frameLatencyWaitable, INFINITE, FALSE);

  // Has the GPU finished the work previously submitted (in this slot)?
  if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
  {
    COM_ERROR_IF_FAILED(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), "Failed to set fence completion event.");
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
  }

  m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();



  /*char buffer[256];
  sprintf_s(
    buffer,
    "fence=%llu slot=%u bb=%u completed=%llu\n",
    m_nextFenceValue,
    m_frameIndex,
    m_backBufferIndex,
    m_fence->GetCompletedValue()
  );

  OutputDebugStringA(buffer);*/
}

void D3D12HelloTriangle::EndFrame()
{
  // Claim the next fence value.
  m_fenceValues[m_frameIndex] = ++m_nextFenceValue;

  // The GPU writes the new fence value once all commands submitted before this point have completed.
  COM_ERROR_IF_FAILED(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]), "Failed to signal command queue fence.");

  m_frameIndex = (m_frameIndex + 1) % kFramesInFlight;
}