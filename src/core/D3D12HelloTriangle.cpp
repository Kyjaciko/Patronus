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
  m_camera(90.f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 1000.f),
  m_timer(),
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
    .SampleDesc = { .Count = 1, .Quality = 0 }, // MSAA turned OFF; flip models don't support MSAA directly on a swap chain!
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = kBufferCount,
    .Scaling = DXGI_SCALING_STRETCH,
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
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
      .NodeMask = 0
    };

    COM_ERROR_IF_FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "Failed to create the descriptor heap.");

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(rtvHeapDesc.Type);

    D3D12_DESCRIPTOR_HEAP_DESC particleSrvUavHeapDesc {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      .NumDescriptors = ParticleHeap::Count,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
      .NodeMask = 0
    };

    COM_ERROR_IF_FAILED(m_device->CreateDescriptorHeap(&particleSrvUavHeapDesc, IID_PPV_ARGS(&m_particleSrvUavHeap)), "Failed to create the particle system descriptor heap.");

    m_particleSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(particleSrvUavHeapDesc.Type);
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

    // Define one SRV slot at register t0 for the structured buffer (m_particlePool).
    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // Put the SRV descriptor and a constant buffer into root parameters and make it visible to the vertex shader.
    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL); // b0
    rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_VERTEX);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

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

    // Simpel shaders for drawing the triangle.
    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(L"shaders_VSMain.cso").c_str(), &pVertexShaderData, &vertexShaderDataLength), "Failed to read the vertex shader.");
    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(L"shaders_PSMain.cso").c_str(), &pPixelShaderData, &pixelShaderDataLength), "Failed to read the pixel shader.");

    UINT8* pParticleVertexShaderData = nullptr;
    UINT8* pParticlePixelShaderData = nullptr;
    UINT particleVertexShaderDataLength = 0;
    UINT particlePixelShaderDataLength = 0;

    // Shaders to draw the particles.
    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(L"particle_shaders_VSMain.cso").c_str(), &pParticleVertexShaderData, &particleVertexShaderDataLength), "Failed to read the particle vertex shader.");
    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(L"particle_shaders_PSMain.cso").c_str(), &pParticlePixelShaderData, &particlePixelShaderDataLength), "Failed to read the particle pixel shader.");

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
  
    D3D12_GRAPHICS_PIPELINE_STATE_DESC particlePsoDesc(psoDesc);
    particlePsoDesc.InputLayout = { nullptr, 0 }; // No vertex input layout, very important since we use SV_VertexID!
    particlePsoDesc.VS = CD3DX12_SHADER_BYTECODE(pParticleVertexShaderData, particleVertexShaderDataLength);
    particlePsoDesc.PS = CD3DX12_SHADER_BYTECODE(pParticlePixelShaderData, particlePixelShaderDataLength);
    COM_ERROR_IF_FAILED(m_device->CreateGraphicsPipelineState(&particlePsoDesc, IID_PPV_ARGS(&m_particlePipelineState)), "Failed to create the particle graphics pipeline state.");

    free(pVertexShaderData);
    free(pPixelShaderData);
  }

  {
    // Define one UAV slot at register u0 for the structured buffer (m_particlePool).
    CD3DX12_DESCRIPTOR_RANGE1 uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    // Put the UAV descriptor into a root parameter and make it visible to the compute shader.
    CD3DX12_ROOT_PARAMETER1 computeRootParameter[2];
    computeRootParameter[0].InitAsConstants(2, 0, 0, D3D12_SHADER_VISIBILITY_ALL); // Bind particle count and delta time to root constant register 0.
    computeRootParameter[1].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
    computeRootSignatureDesc.Init_1_1(_countof(computeRootParameter), computeRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    COM_ERROR_IF_FAILED(D3DX12SerializeVersionedRootSignature(&computeRootSignatureDesc, featureData.HighestVersion, &signature, &error), "Failed to serialize the compute root signature.");
    COM_ERROR_IF_FAILED(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_computeRootSignature)), "Failed to create the compute root signature.");
  }

  {
    UINT8* pComputeShaderData = nullptr;
    UINT computeShaderDataLength = 0;

    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(L"shaders_CSMain.cso").c_str(), &pComputeShaderData, &computeShaderDataLength), "Failed to read the compute shader.");
   
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc {
      .pRootSignature = m_computeRootSignature.Get(),
      .CS = CD3DX12_SHADER_BYTECODE(pComputeShaderData, computeShaderDataLength),
      .NodeMask = 0,
      .CachedPSO = { nullptr, 0 },
      .Flags = D3D12_PIPELINE_STATE_FLAG_NONE
    };

    COM_ERROR_IF_FAILED(m_device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_computePipelineState)), "Failed to create the compute pipeline state.");
  
    free(pComputeShaderData);
  }

  // Create the command list.
  COM_ERROR_IF_FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)), "Failed to create the command list.");

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
        IID_PPV_ARGS(&m_vertexBuffer)
      ), 
      "Failed to create the vertex buffer."
    );

    const UINT particlePoolSize = sizeof(Particle) * kParticleCount;

    // Place the particle pool (structured buffer) in the Default Heap.
    COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(particlePoolSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_particlePool)
      ), 
      "Failed to create the particle pool structured buffer."
    );

    // Upload heap to write data to the Default Heap (particle pool).
    COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(particlePoolSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_particleUploadBuffer)
      ), 
      "Failed to create the vertex buffer."
    );

    // Copy data to upload heap for particle system.
    {
      Particle* pParticleDataBegin = nullptr;
      CD3DX12_RANGE readRange(0, 0);
      COM_ERROR_IF_FAILED(m_particleUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pParticleDataBegin)), "Failed to map the upload heap for the particle system.");

      for (UINT i = 0; i < kParticleCount; ++i)
      {
        constexpr float spacing = 0.15f;
        constexpr UINT columns = 25;

        const UINT column = i % columns;
        const UINT row    = i / columns;

        const float x = (static_cast<float>(column) - (columns - 1) * 0.5f) * spacing;
        const float y = (static_cast<float>(row) - (columns - 1) * 0.5f) * spacing;

        pParticleDataBegin[i] = {
          .pos = { x, y, -5.f},
          .vel = { 2.f, 5.f, 0.f },
          .lifetime = 10.f // Seconds.
        };
      }

      m_particleUploadBuffer->Unmap(0, nullptr);
    }

    // The constant buffer size (and in that regard also the address) needs to be a multiple of 256 bytes!
    static constexpr UINT cameraSizeCB = (sizeof(CameraCB) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

    // Copy camera data to constant buffer for particle system.
    COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(cameraSizeCB),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_cameraCB)
      ), 
      "Failed to create the vertex buffer."
    );

    {
      CameraCB* pCameraDataBegin = nullptr;
      CD3DX12_RANGE readRange(0, 0);
      COM_ERROR_IF_FAILED(m_cameraCB->Map(0, &readRange, reinterpret_cast<void**>(&pCameraDataBegin)), "Failed to map the constant buffer that holds the camera data.");

      DirectX::XMStoreFloat4x4(&pCameraDataBegin->viewProj, m_camera.GetViewMatrix() * m_camera.GetProjectionMatrix());
      DirectX::XMStoreFloat3(&pCameraDataBegin->camRight, m_camera.GetRightVector());
      pCameraDataBegin->billboardSize = 0.05f;
      DirectX::XMStoreFloat3(&pCameraDataBegin->camUp, m_camera.GetUpVector());

      m_cameraCB->Unmap(0, nullptr);
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc {
      .Format = DXGI_FORMAT_UNKNOWN,
      .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
      .Buffer = {
        .FirstElement = 0,
        .NumElements = kParticleCount,
        .StructureByteStride = sizeof(Particle),
        .CounterOffsetInBytes = 0,
        .Flags = D3D12_BUFFER_UAV_FLAG_NONE
      }
    };

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {
      .Format = DXGI_FORMAT_UNKNOWN,
      .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
      .Buffer = {
        .FirstElement = 0,
        .NumElements = kParticleCount,
        .StructureByteStride = sizeof(Particle),
        .Flags = D3D12_BUFFER_SRV_FLAG_NONE
      }
    };

    // Create the UAV for the compute shader.
    CD3DX12_CPU_DESCRIPTOR_HANDLE particleHeap(m_particleSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(particleHeap, ParticleHeap::PoolUAV, m_particleSrvUavDescriptorSize);
    m_device->CreateUnorderedAccessView(m_particlePool.Get(), nullptr, &uavDesc, uavHandle);
    
    // Create the SRV for the vertex shader.
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(particleHeap, ParticleHeap::PoolSRV, m_particleSrvUavDescriptorSize);
    m_device->CreateShaderResourceView(m_particlePool.Get(), &srvDesc, srvHandle);

    // Copy the triangle data to the vertex buffer.
    UINT8* pVertexDataBegin = nullptr;
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
  m_timer.Update();
  m_particleSimConstants.deltaTime = static_cast<float>(m_timer.GetDeltaTime());
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
  if (m_VSync)
    COM_ERROR_IF_FAILED(m_swapChain->Present(1, 0), "Failed to present the frame with VSync ON.");
  else
  {
    // When VSync is disabled, use DXGI_PRESENT_ALLOW_TEARING when supported.
    // The tearing flag cannot be used for fullscreen mode entered through
    // SetFullscreenState.
    UINT present_flags = (m_tearingSupport && m_windowedMode) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    COM_ERROR_IF_FAILED(m_swapChain->Present(0, present_flags), "Failed to present the frame.");
  }

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
  m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

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

  wchar_t updatedTitle[256];
  swprintf_s(updatedTitle, L"( %u x %u )", m_width, m_height);
  SetCustomWindowText(updatedTitle);

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
    // TODO: When Present(), ResizeBuffers etc. fails, COM_ERROR_IF_FAILED terminates
    // the process immediately using exit(-1). Exclusive fullscreen
    // is left un-exited which is a known way to leave the driver in a bad state.

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

    break;
  }

  default:
    break;

  }
}

void D3D12HelloTriangle::PopulateCommandList()
{
  // Safe here because BeginFrame() waited on m_fenceValues[m_frameIndex].
  COM_ERROR_IF_FAILED(m_commandAllocators[m_frameIndex]->Reset(), "Failed to reset the command allocator.");
  COM_ERROR_IF_FAILED(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr), "Failed to reset the command list.");

  // Copy data from upload heap (filled by CPU) to Default Heap.
  static bool firstRun = false;
  if (!firstRun)
  {
    m_commandList->CopyResource(m_particlePool.Get(), m_particleUploadBuffer.Get());

    // Change Default Heap (m_particlePool) from COPY_DEST to UNORDERED_ACCESS.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_particlePool.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
  
    firstRun = true;
  }

  // Compute pass.
  ID3D12DescriptorHeap* ppHeaps[] = { m_particleSrvUavHeap.Get() };
  m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

  CD3DX12_GPU_DESCRIPTOR_HANDLE particleHeap(m_particleSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
  CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(particleHeap, ParticleHeap::PoolUAV, m_particleSrvUavDescriptorSize);

  m_commandList->SetPipelineState(m_computePipelineState.Get());
  m_commandList->SetComputeRootSignature(m_computeRootSignature.Get());
  m_commandList->SetComputeRoot32BitConstants(0, 2, &m_particleSimConstants, 0);
  m_commandList->SetComputeRootDescriptorTable(1, uavHandle);

  constexpr UINT threadGroupCountX = (kParticleCount + 255) / 256; // Round up NOT down.
  m_commandList->Dispatch(threadGroupCountX, 1, 1);

  // Change Default Heap (m_particlePool) from UNORDERED_ACCESS to SHADER_RESOURCE.
  m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_particlePool.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

  // Graphics pass.
  m_commandList->SetPipelineState(m_pipelineState.Get());
  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->RSSetViewports(1, &m_viewport);
  m_commandList->RSSetScissorRects(1, &m_scissorRect);

  // Indicate that the back buffer will be used as a render target.
  m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_backBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_backBufferIndex, m_rtvDescriptorSize);
  m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

  // Draw triangle.
  const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
  m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
  m_commandList->DrawInstanced(3, 1, 0, 0);

  // Draw particles.
  m_commandList->SetPipelineState(m_particlePipelineState.Get());
  //m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->SetGraphicsRootConstantBufferView(0, m_cameraCB->GetGPUVirtualAddress());
  CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(particleHeap, ParticleHeap::PoolSRV, m_particleSrvUavDescriptorSize);
  m_commandList->SetGraphicsRootDescriptorTable(1, srvHandle);
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  m_commandList->DrawInstanced(4, kParticleCount, 0, 0);

  // Change Default Heap (m_particlePool) from SHADER_RESOURCE to UNORDERED_ACCESS.
  m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_particlePool.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

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