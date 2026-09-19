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
  m_frameNumber(0),
  m_windowVisible(true),
  m_windowedMode(true)
{
}

D3D12HelloTriangle::~D3D12HelloTriangle()
{
  m_timestampWriter.Close();

  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
}

void D3D12HelloTriangle::OnInit()
{
  LoadPipeline();
  LoadAssets();

  // Setup Dear ImGui.
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Allow Dear ImGui to be outside our window.

  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  /*style.ScaleAllSizes(dpi_scale); // TODO: Implement DPI aware ImGui.
  style.FontScaleDpi = dpi_scale;
  io.ConfigDpiScaleFonts = true;
  io.ConfigDpiScaleViewports = true;*/

  // When viewports are enabled we tweak the style so platform windows can look identical to regular ones.
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    style.WindowRounding = 0.f;
    style.Colors[ImGuiCol_WindowBg].w = 1.f;
  }

  ImGui_ImplWin32_Init(Win32Application::GetHwnd());
  ImGui_ImplDX12_InitInfo initInfo{};
  initInfo.Device = m_device.Get();
  initInfo.CommandQueue = m_commandQueue.Get();
  initInfo.NumFramesInFlight = kFramesInFlight;
  initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // See: DXGI_SWAP_CHAIN_DESC1 swapChainDesc.
  initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;        // No depth buffer currently.
  initInfo.UserData = nullptr;
  initInfo.SrvDescriptorHeap = m_particleSrvUavHeap.Get();
  initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle) {
    ID3D12DescriptorHeap* srvHeap = info->SrvDescriptorHeap;
    UINT descriptorSize = info->Device->GetDescriptorHandleIncrementSize(srvHeap->GetDesc().Type);

    *cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12HelloTriangle::ParticleHeap::DearImGui, descriptorSize);
    *gpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvHeap->GetGPUDescriptorHandleForHeapStart(), D3D12HelloTriangle::ParticleHeap::DearImGui, descriptorSize);
  };
  initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    // Permanently reserve a slot for Dear ImGui.
  };
  ImGui_ImplDX12_Init(&initInfo);
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
    GetHardwareAdapter(factory.Get(), &m_hardwareAdapter);

    COM_ERROR_IF_FAILED(D3D12CreateDevice(
        m_hardwareAdapter.Get(),
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
  COM_ERROR_IF_FAILED(m_commandQueue->GetTimestampFrequency(&m_timestampFrequency), "Failed to fetch the GPU timestamp counter frequency.");

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
    // Define one UAV slot at register u0 for the structured buffer (m_particlePool)
    // and one SRV slot at register t0 for the curl noise texture.
    CD3DX12_DESCRIPTOR_RANGE1 srvUavRange[2];
    srvUavRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    srvUavRange[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // Put the UAV descriptor into a root parameter and make it visible to the compute shader.
    CD3DX12_ROOT_PARAMETER1 computeRootParameter[2];
    computeRootParameter[0].InitAsConstants(2, 0, 0, D3D12_SHADER_VISIBILITY_ALL); // Bind particle count and delta time to root constant register 0.
    computeRootParameter[1].InitAsDescriptorTable(2, srvUavRange, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_STATIC_SAMPLER_DESC sampler {
      .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
      .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      .MipLODBias = 0,
      .MaxAnisotropy = 0,
      .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
      .BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
      .MinLOD = 0.f,
      .MaxLOD = D3D12_FLOAT32_MAX,
      .ShaderRegister = 0, // Register s0.
      .RegisterSpace = 0,
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
    computeRootSignatureDesc.Init_1_1(_countof(computeRootParameter), computeRootParameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

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
  //COM_ERROR_IF_FAILED(m_commandList->Close(), "Failed to close the command list.");

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

    // Create the index buffer.
    {
      static constexpr UINT kVerticesPerParticle = 4;
      static constexpr UINT kIndicesPerParticle  = 6;
      const UINT indexCount = kIndicesPerParticle * kParticleCount;

      std::vector<uint32_t> indices(indexCount);
      for (UINT i = 0; i < kParticleCount; ++i)
      {
        // Each particle has it's own 4 vertices (square).
        const uint32_t baseVertexId = i * kVerticesPerParticle;

        // See: particleshader.hlsl for order.
        // Triangle 1: tl, tr, bl.
        indices[i * kIndicesPerParticle + 0] = baseVertexId + 0;
        indices[i * kIndicesPerParticle + 1] = baseVertexId + 1;
        indices[i * kIndicesPerParticle + 2] = baseVertexId + 2;

        // Triangle 2: bl, tr, br.
        indices[i * kIndicesPerParticle + 3] = baseVertexId + 2;
        indices[i * kIndicesPerParticle + 4] = baseVertexId + 1;
        indices[i * kIndicesPerParticle + 5] = baseVertexId + 3;
      }

      const UINT indexBufferSize = indexCount * sizeof(uint32_t);

      COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
          D3D12_RESOURCE_STATE_GENERIC_READ,
          nullptr,
          IID_PPV_ARGS(&m_indexUploadBuffer)
        ), 
        "Failed to create the index upload buffer."
      );

      COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
          D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,
          IID_PPV_ARGS(&m_indexBuffer)
        ), 
        "Failed to create the index default buffer."
      );

      // Copy the data to the index upload buffer.
      UINT8* pIndexDataBegin = nullptr;
      CD3DX12_RANGE readRange(0, 0);
      COM_ERROR_IF_FAILED(m_indexUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)), "Failed to map the index buffer.");
      memcpy(pIndexDataBegin, indices.data(), indexBufferSize);
      m_indexUploadBuffer->Unmap(0, nullptr);

      // Copy the data from upload to the default buffer.
      m_commandList->CopyResource(m_indexBuffer.Get(), m_indexUploadBuffer.Get());

      // Change Default Heap (m_indexBuffer) from COPY_DEST to INDEX_BUFFER.
      m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));
    
      m_indexBufferView = {
        .BufferLocation = m_indexBuffer->GetGPUVirtualAddress(),
        .SizeInBytes = indexBufferSize,
        .Format = DXGI_FORMAT_R32_UINT  // uint32_t = R32_UINT.
      };
    }

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

      // Grid formation.
      /*for (UINT i = 0; i < kParticleCount; ++i)
      {
        constexpr float spacing = 0.15f;
        constexpr UINT columns = 25;

        const UINT column = i % columns;
        const UINT row    = i / columns;

        const float x = (static_cast<float>(column) - (columns - 1) * 0.5f) * spacing;
        const float y = (static_cast<float>(row) - (columns - 1) * 0.5f) * spacing;

        pParticleDataBegin[i] = {
          .pos = { x, y, -5.f},
          .vel = { 0.f, 0.f, 0.f },
          .lifetime = 10.f // Seconds.
        };
      }*/

      // Random placement on a plane parallel to the XY-plane.
      /*static std::mt19937 rng{ std::random_device{}() };

      static constexpr float fovY = DirectX::XMConvertToRadians(90.f);
      static constexpr float aspectRatio = 1280.f / 720.f;
      static constexpr float z = 5.f;

      static const float halfHeightWindow = z * std::tan(fovY * 0.5f);
      static const float halfWidthWindow = halfHeightWindow * aspectRatio;

      static std::uniform_real_distribution<float> distX(-halfWidthWindow, halfWidthWindow);
      static std::uniform_real_distribution<float> distY(-halfHeightWindow, halfHeightWindow);

      for (UINT i = 0; i < kParticleCount; ++i)
      {
        const float x = distX(rng);
        const float y = distY(rng);

        pParticleDataBegin[i] = {
          .pos = { x, y, -z},
          .vel = { 0.f, 0.f, 0.f },
          .lifetime = 10.f // Seconds.
        };
      }*/

      // Thin oval placement.
      /*static std::mt19937 rng{ std::random_device{}() };

      static constexpr XMFLOAT2 ovalRadius = { 7.f, 0.5f };
      static constexpr float thickness = 0.25f;
      static constexpr float z = 5.f;

      static std::uniform_real_distribution<float> distAngle(0, DirectX::XM_2PI);
      static std::uniform_real_distribution<float> distThickness(-thickness, thickness);

      for (UINT i = 0; i < kParticleCount; ++i)
      {
        const float theta = distAngle(rng);
        const float jitter = distThickness(rng);

        const float x = (ovalRadius.x + jitter) * std::cos(theta);
        const float y = (ovalRadius.y + jitter) * std::sin(theta);

        pParticleDataBegin[i] = {
          .pos = { x, y, -z},
          .vel = { 0.f, 0.f, 0.f },
          .lifetime = 10.f // Seconds.
        };
      }*/

      // Uniformly distributed sphere placement,
      // based on Deserno's method for equidistant points on a sphere (2004).
      {
        static std::mt19937 rng{ std::random_device{}() };

        static constexpr XMFLOAT3 center = { 0.f, 0.f, 0.f };
        static constexpr XMFLOAT3 axis = { 0.f, 1.f, 0.f };
        static constexpr float radius = 5.f;

        // Tilt axis around the z-axis.
        const float angleRad = XMConvertToRadians(-30.f);
        const XMVECTOR tiltedAxis = XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&axis), XMMatrixRotationZ(angleRad)));

        XMFLOAT3 tiltedAxisFloat3;
        XMStoreFloat3(&tiltedAxisFloat3, tiltedAxis);
        
        static std::uniform_real_distribution<float> distUnit(0.f, 1.f);

        // See: Duff et al. (2017), "Building an Orthonormal Basis, Revisited"
        constexpr auto BuildOrthonormalBasis = [](const XMFLOAT3& n) -> std::pair<XMFLOAT3, XMFLOAT3>
        {
          const float sign = std::copysignf(1.0f, n.z);
          const float a = -1.0f / (sign + n.z);
          const float b = n.x * n.y * a;

          const XMFLOAT3 u{ 1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x };
          const XMFLOAT3 v{ b, sign + n.y * n.y * a, -n.y };

          return { u, v };
        };

        const auto [u, v] = BuildOrthonormalBasis(tiltedAxisFloat3);
        const XMVECTOR centerVec = XMLoadFloat3(&center);
        const XMVECTOR uVec = XMLoadFloat3(&u);
        const XMVECTOR vVec = XMLoadFloat3(&v);
        const XMVECTOR nVec = XMLoadFloat3(&tiltedAxisFloat3);

        // Determine the circle count the sphere is made out of.
        const float N = static_cast<float>(kParticleCount);

        const float areaPerParticle = (4.f * XM_PI * radius * radius) / N;
        const float d = std::sqrt(areaPerParticle);

        const float dTheta = XM_PI * radius / std::round(XM_PI * radius / d);
        const float dPhi = areaPerParticle / dTheta;

        const UINT ringCount = std::max<UINT>(1u, static_cast<UINT>(std::round(XM_PI * radius / dTheta)));
        const float phaseOffsetGlobal = distUnit(rng) * XM_2PI;

        UINT writeIndex = 0;
        for (UINT i = 0; i < ringCount; ++i)
        {
          const float theta = XM_PI * (static_cast<float>(i) + 0.5f) / static_cast<float>(ringCount);
          const float ringRadius = radius * std::sin(theta);
          const float ringHeight = radius * std::cos(theta);

          const float circumference = XM_2PI * ringRadius;
          const UINT particlesInRing = std::max<UINT>(1u, static_cast<UINT>(std::round(circumference / dPhi)));

          const float phaseOffset = phaseOffsetGlobal + distUnit(rng) * XM_2PI;

          for (UINT j = 0; j < particlesInRing; ++j)
          {
            const float phi = phaseOffset + XM_2PI * static_cast<float>(j) / static_cast<float>(particlesInRing);

            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);

            const XMVECTOR offset = XMVectorAdd(XMVectorScale(nVec, ringHeight), XMVectorAdd(XMVectorScale(uVec, ringRadius * cosPhi), XMVectorScale(vVec, ringRadius * sinPhi)));

            XMFLOAT3 pos;
            XMStoreFloat3(&pos, XMVectorAdd(centerVec, offset));

            pParticleDataBegin[writeIndex++] = {
              .pos = pos,
              .vel = { 0.f, 0.f, 0.f },
              .lifetime = 10.f // Seconds.
            };
          }
        }
      }

      m_particleUploadBuffer->Unmap(0, nullptr);

      m_camera.SetPosition(0.f, 0.f, 20.f);
    }

    m_commandList->CopyResource(m_particlePool.Get(), m_particleUploadBuffer.Get());

    // Change Default Heap (m_particlePool) from COPY_DEST to UNORDERED_ACCESS.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_particlePool.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    // The constant buffer size (and in that regard also the address) needs to be a multiple of 256 bytes!
    static constexpr UINT cameraSizeCB = (sizeof(CameraCB) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

    // Copy camera data to constant buffer for particle system.
    for (std::ptrdiff_t i = 0; i < _countof(m_cameraCB); ++i)
    {
      COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(cameraSizeCB),
          D3D12_RESOURCE_STATE_GENERIC_READ,
          nullptr,
          IID_PPV_ARGS(&m_cameraCB[i])
        ), 
        L"Failed to create the camera constant buffer at index: " + std::to_wstring(i) + L"."
      );

      UpdateCameraCB(m_cameraCB[i]);
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

  // Create the curl noise texture.
  {
    // Describe and create a Texture3D.
    D3D12_RESOURCE_DESC curlNoiseTextureDesc {
      .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D,
      .Width = 64, // 64x64x64 is ideal, since this is only 2MB and will therefor be within the L2 cache.
      .Height = 64,
      .DepthOrArraySize = 64,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
      .SampleDesc = { .Count = 1, .Quality = 0 },
      .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN, // Let drivers optimize memory layout.
      .Flags = D3D12_RESOURCE_FLAG_NONE
    };

    // Place the curl noise texture in the Default Heap.
    COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &curlNoiseTextureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_curlNoiseTextureHeap)
      ), 
      "Failed to create the curl noise texture buffer."
    );

    UINT64 rawCurlNoiseSize = GetRequiredIntermediateSize(m_curlNoiseTextureHeap.Get(), 0, 1);

    // Upload heap to write data to the Default Heap (curl noise texture).
    COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(rawCurlNoiseSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_rawCurlNoiseDataHeap)
      ), 
      "Failed to create the raw curl noise (.bin) buffer."
    );

    // Copy raw curl noise data to upload heap.
    {
      // Read raw curl noise data from bin file.
      std::vector<UINT8> m_rawCurlNoiseData(rawCurlNoiseSize);
      std::ifstream file(GetAssetFullPath(L"assets/noise/old curl noise/curl_noise_64x64x64_rgba16f_type2.bin"), std::ios::binary);
      //std::ifstream file(GetAssetFullPath(L"assets/noise/curl_noise_64_rgba16f.bin"), std::ios::binary);
      if (!file.is_open()) COM_ERROR_IF_FAILED(E_FAIL, "Failed to read file: curl_noise_64x64x64_rgba16f_type2.bin.");
      
      // Get length of the bin file.
      file.seekg(0, std::ios::end);
      std::streamsize fileLength = file.tellg();
      file.seekg(0, std::ios::beg);

      // Data length of the bin file should match the size of the upload heap.
      if (fileLength != static_cast<std::streamsize>(rawCurlNoiseSize)) COM_ERROR_IF_FAILED(E_FAIL, "Size of upload heap and curl noise raw data bin file don't match.");
      file.read(reinterpret_cast<char*>(m_rawCurlNoiseData.data()), static_cast<std::streamsize>(m_rawCurlNoiseData.size()));
      
      if (!file) COM_ERROR_IF_FAILED(E_FAIL, "Failed to read raw curl noise data from bin file.");
      file.close();

      // Specifies the size of a single depth slice for UpdateSubresources.
      D3D12_SUBRESOURCE_DATA curlNoiseTextureData {
        .pData = m_rawCurlNoiseData.data(),
        .RowPitch = static_cast<LONG_PTR>(curlNoiseTextureDesc.Width * kTexturePixelSize),
        .SlicePitch = static_cast<LONG_PTR>(curlNoiseTextureData.RowPitch * curlNoiseTextureDesc.Height)
      };

      // UpdateSubresources makes sure we obey the 256 byte aligment.
      UpdateSubresources(m_commandList.Get(), m_curlNoiseTextureHeap.Get(), m_rawCurlNoiseDataHeap.Get(), 0, 0, 1, &curlNoiseTextureData);
      m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_curlNoiseTextureHeap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

      // Create the SRV for the curl noise texture.
      D3D12_SHADER_RESOURCE_VIEW_DESC srvCurlNoiseDesc {
        .Format = curlNoiseTextureDesc.Format,
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture3D = {
          .MostDetailedMip = 0,
          .MipLevels = curlNoiseTextureDesc.MipLevels,
          .ResourceMinLODClamp = 0.f
        }
      };

      CD3DX12_CPU_DESCRIPTOR_HANDLE srvCurlNoiseHandle(m_particleSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), ParticleHeap::CurlNoiseSRV, m_particleSrvUavDescriptorSize);
      m_device->CreateShaderResourceView(m_curlNoiseTextureHeap.Get(), &srvCurlNoiseDesc, srvCurlNoiseHandle);
    }
  }

  // Close the command list and execute it to begin the initial GPU setup.
  COM_ERROR_IF_FAILED(m_commandList->Close(), "Failed to close the command list.");
  ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
  m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  // Create the timestamps readback buffer.
  {
    D3D12_QUERY_HEAP_DESC timestampQueryHeapDesc {
      .Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
      .Count = kFramesInFlight * kSlotsPerFrame,
      .NodeMask = 0
    };
    COM_ERROR_IF_FAILED(m_device->CreateQueryHeap(&timestampQueryHeapDesc, IID_PPV_ARGS(&m_timestampQueryHeap)), "Failed to create the timestamp query heap.");

    static const UINT timestampQueryHeapSize = timestampQueryHeapDesc.Count * sizeof(UINT64);

    COM_ERROR_IF_FAILED(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(timestampQueryHeapSize),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_timestampQueryResult)
      ), 
      L"Failed to create the timestamp readback buffer."
    );
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

  // Start logging the GPU timestamps to a CSV file.
  {
    // Get adapter name.
    DXGI_ADAPTER_DESC1 adapterDesc;
    m_hardwareAdapter->GetDesc1(&adapterDesc);

    patronus::profiling::FrameTimingWriter::Metadata metadata {
      {"gpu", StringHelper::WideToString(adapterDesc.Description)},
      {"config", PATRONUS_BUILD_CONFIG},
      {"resolution", std::to_string(m_width) + "x" + std::to_string(m_height)},
      {"particles", std::to_string(kParticleCount)},
      {"note", "Curl noise ORB using DrawIndexedInstanced(6N, 1)"}
    };
    
    if (!m_timestampWriter.IsOpen())
      m_timestampWriter.Open("benchmarks/runs/gpu_timestamps_curl_noise_drawindexedinstanced_6N_1.csv", metadata);
  }

  // Release the raw curl noise data, since it's now on the GPU (in the default heap).
  m_rawCurlNoiseDataHeap.Reset(); // Allowed to reset here since WaitForGpu() is called before.
  m_particleUploadBuffer.Reset(); //
  m_indexUploadBuffer.Reset();
}

// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
  // Enable Dear ImGui recording.
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  static bool show_demo_window = true;
  if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

  m_timer.Update();
  m_particleSimConstants.deltaTime = std::min(static_cast<float>(m_timer.GetDeltaTime()), kMaxDeltaTime);

  // Autorepeat is off, the character is queued once per physical press.
  while (!m_keyboard->IsCharBufferEmpty())
  {
    unsigned char character = m_keyboard->ReadChar();

    if (character == '&')
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

  // Autorepeat is off, one KEY_DOWN per physical press.
  while (!m_keyboard->IsKeyBufferEmpty())
  {
    KeyboardEvent event = m_keyboard->ReadKey();
    unsigned char key = event.GetKey();

    // Instrument TAB to toggle between fullscreen states.
    // The window message loop callback will receive a WM_SIZE message once the
    // window is in the fullscreen state. At that point, the IDXGISwapChain should
    // be resized to match the new window size
    if (key == VK_TAB && event.IsPressed())
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
    else if (key == 'V' && event.IsPressed())
    {
      m_VSync = !m_VSync;
    }
  }

  // Camera rotation tracks mouse movement while right button is held.
  while (!m_mouse->IsEventBufferEmpty())
  {
    MouseEvent event = m_mouse->ReadEvent();
    if (m_mouse->IsRightPressed() && event.GetEventType() == MouseEvent::EventType::RAW_MOVE)
    {
      static constexpr float sensitivity = 0.001f;
      static constexpr float maxPitch    = XM_PIDIV2 - 0.01f;

      m_camera.AdjustRotation(
        static_cast<float>(event.GetPosY()) * sensitivity,
        static_cast<float>(event.GetPosX()) * sensitivity, 
        0.f
      );

      // Avoid going upside down.
      XMFLOAT3 cameraRotation = m_camera.GetRotationFloat3();
      m_camera.SetRotation(std::clamp(cameraRotation.x, -maxPitch, maxPitch), cameraRotation.y, cameraRotation.z);
    }
  }

  // Key state is sampled every frame.
  float cameraSpeed = 10.f;
  if (m_keyboard->IsKeyPressed(VK_SPACE))
		cameraSpeed = 100.0f;
  if (m_keyboard->IsKeyPressed('Z'))
    m_camera.AdjustPosition(m_camera.GetForwardVector() * cameraSpeed * m_particleSimConstants.deltaTime);
  if (m_keyboard->IsKeyPressed('S'))
    m_camera.AdjustPosition(m_camera.GetBackwardVector() * cameraSpeed * m_particleSimConstants.deltaTime);
  if (m_keyboard->IsKeyPressed('Q'))
    m_camera.AdjustPosition(m_camera.GetLeftVector() * cameraSpeed * m_particleSimConstants.deltaTime);
  if (m_keyboard->IsKeyPressed('D'))
    m_camera.AdjustPosition(m_camera.GetRightVector() * cameraSpeed * m_particleSimConstants.deltaTime);
  if (m_keyboard->IsKeyPressed(VK_SHIFT))
    m_camera.AdjustPosition(0.f, cameraSpeed * m_particleSimConstants.deltaTime, 0.f);
  if (m_keyboard->IsKeyPressed(VK_CONTROL))
    m_camera.AdjustPosition(0.f, -cameraSpeed * m_particleSimConstants.deltaTime, 0.f);
}

// Render the scene.
void D3D12HelloTriangle::OnRender()
{
  if (!m_windowVisible)
    return;

  BeginFrame();

  // Must be after BeginFrame() 'cause if we're waiting on the fence the GPU is handling frame N-2 
  // and we would otherwise write to the same CB the GPU is reading from.
  UpdateCameraCB(m_cameraCB[m_frameIndex]);

  // Capture all Dear ImGui draw data.
  ImGui::Render();

  // Record all the commands we need to render the scene into the command list.
  PopulateCommandList();

  // Execute the command list.
  ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
  m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  // Update and render additional platform windows.
  ImGuiIO& io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }

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
  m_camera.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));

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

void D3D12HelloTriangle::UpdateCameraCB(const ComPtr<ID3D12Resource>& camera_constant_buffer)
{
  CameraCB* pCameraDataBegin = nullptr;
  CD3DX12_RANGE readRange(0, 0);
  COM_ERROR_IF_FAILED(camera_constant_buffer->Map(0, &readRange, reinterpret_cast<void**>(&pCameraDataBegin)), "Failed to map the constant buffer that holds the camera data.");

  DirectX::XMStoreFloat4x4(&pCameraDataBegin->viewProj, m_camera.GetViewMatrix() * m_camera.GetProjectionMatrix());
  DirectX::XMStoreFloat3(&pCameraDataBegin->camRight, m_camera.GetRightVector());
  pCameraDataBegin->billboardSize = 0.05f;
  DirectX::XMStoreFloat3(&pCameraDataBegin->camUp, m_camera.GetUpVector());

  camera_constant_buffer->Unmap(0, nullptr);
}

void D3D12HelloTriangle::PopulateCommandList()
{
  const UINT queryBase = m_frameIndex * kSlotsPerFrame;

  // Safe here because BeginFrame() waited on m_fenceValues[m_frameIndex].
  COM_ERROR_IF_FAILED(m_commandAllocators[m_frameIndex]->Reset(), "Failed to reset the command allocator.");
  COM_ERROR_IF_FAILED(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr), "Failed to reset the command list.");

  m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryBase + TimestampSlots::FRAME_BEGIN);

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
  m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryBase + TimestampSlots::SIM_BEGIN);
  m_commandList->Dispatch(threadGroupCountX, 1, 1);
  m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryBase + TimestampSlots::SIM_END);

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

  // Render triangle.
  const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
  m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
  m_commandList->DrawInstanced(3, 1, 0, 0);

  // Render particles.
  m_commandList->SetPipelineState(m_particlePipelineState.Get());
  //m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->SetGraphicsRootConstantBufferView(0, m_cameraCB[m_frameIndex]->GetGPUVirtualAddress());
  CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(particleHeap, ParticleHeap::PoolSRV, m_particleSrvUavDescriptorSize);
  m_commandList->SetGraphicsRootDescriptorTable(1, srvHandle);
  //m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // DrawIndexedInstanced()
  m_commandList->IASetIndexBuffer(&m_indexBufferView); // DrawIndexedInstanced()
  m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryBase + TimestampSlots::RENDER_BEGIN);
  //m_commandList->DrawInstanced(4, kParticleCount, 0, 0);
  m_commandList->DrawIndexedInstanced(6 * kParticleCount, 1, 0, 0, 0);
  m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryBase + TimestampSlots::RENDER_END);

  // Render Dear ImGui.
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

  // Change Default Heap (m_particlePool) from SHADER_RESOURCE to UNORDERED_ACCESS.
  m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_particlePool.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

  // Indicate that the back buffer will now be used to present.
  m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_backBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
  
  m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryBase + TimestampSlots::FRAME_END);
  m_commandList->ResolveQueryData(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryBase, kSlotsPerFrame, m_timestampQueryResult.Get(), static_cast<UINT64>(queryBase) * sizeof(UINT64));

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

void D3D12HelloTriangle::ReadTimestamps()
{
  if (m_frameNumber < kFramesInFlight)
    return;

  // Determine where to start and end reading, since it's a readback buffer.
  const UINT queryBase = m_frameIndex * kSlotsPerFrame;
  const SIZE_T byteBegin = static_cast<SIZE_T>(queryBase) * sizeof(UINT64);
  const SIZE_T byteEnd = byteBegin + static_cast<SIZE_T>(kSlotsPerFrame) * sizeof(UINT64);

  UINT64* mappedSlots = nullptr;
  CD3DX12_RANGE readRange(byteBegin, byteEnd);
  COM_ERROR_IF_FAILED(m_timestampQueryResult->Map(0, &readRange, reinterpret_cast<void**>(&mappedSlots)), "Failed to map the buffer that holds the query time stamps.");

  const UINT64* slots = mappedSlots + queryBase;

  double msFrame  = patronus::utils::TicksToMilliseconds(slots[TimestampSlots::FRAME_BEGIN], slots[TimestampSlots::FRAME_END], m_timestampFrequency);
  double msSim    = patronus::utils::TicksToMilliseconds(slots[TimestampSlots::SIM_BEGIN], slots[TimestampSlots::SIM_END], m_timestampFrequency);
  double msRender = patronus::utils::TicksToMilliseconds(slots[TimestampSlots::RENDER_BEGIN], slots[TimestampSlots::RENDER_END], m_timestampFrequency);

  CD3DX12_RANGE writeRange(0, 0);
  m_timestampQueryResult->Unmap(0, &writeRange);

  m_timestampWriter.Add(m_frameNumber - kFramesInFlight, "frame", msFrame);
  m_timestampWriter.Add(m_frameNumber - kFramesInFlight, "sim", msSim);
  m_timestampWriter.Add(m_frameNumber - kFramesInFlight, "render", msRender);
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

  ReadTimestamps();

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

  ++m_frameNumber;
}