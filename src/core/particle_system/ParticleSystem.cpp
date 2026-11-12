#include "ParticleSystem.h"

namespace patronus::effects
{

  ParticleSystem::ParticleSystem()
    : system_state_{.duration = 10.f}
    , emitter_state_{.rate_over_time = 100'000.f}
    , emitter_constants_{}
    , cbv_srv_uav_descriptor_size_{}
    , cbv_srv_uav_heap_{}
    , upload_buffer_{}
    , emitter_constant_buffer_{}
    , counter_{}
    , dead_list_{}
    //, alive_list_{}
    , cs_root_signature_{}
    , kickoff_cs_pipeline_state_{}
    , emitter_cs_pipeline_state_{}
  {
  }

  ParticleSystem::~ParticleSystem()
  {
    // Release the COM pointers.
    cbv_srv_uav_heap_.Reset();
    upload_buffer_.Reset();
    for (uint32_t i = 0; i < _countof(emitter_constant_buffer_); ++i) emitter_constant_buffer_[i].Reset();
    counter_.Reset();
    dead_list_.Reset();
    cs_root_signature_.Reset();
    kickoff_cs_pipeline_state_.Reset();
    emitter_cs_pipeline_state_.Reset();
  }

  _Use_decl_annotations_
  void ParticleSystem::Init(ID3D12Device* const device, ID3D12GraphicsCommandList* const command_list, ID3D12Resource* const particle_pool)
  {
    CreateConstantBuffers(device);
    CreateLists(device, command_list, particle_pool);
    CreateSignature(device);
    CreatePipelineStates(device);
  }

  _Use_decl_annotations_
  void ParticleSystem::Update(const float delta_time)
  {
    system_state_.time += delta_time;

    uint32_t emit_request{};
    bool     active = system_state_.time >= system_state_.delay &&
                      system_state_.time <  system_state_.delay + system_state_.duration;

    if (active)
    {
      system_state_.accumulator += emitter_state_.rate_over_time * delta_time;
      emit_request              = static_cast<uint32_t>(system_state_.accumulator);
      system_state_.accumulator -= emit_request;
    }
    else if (system_state_.looping)
    {
      system_state_.time        = 0.f;
      system_state_.accumulator = 0.f;
    }

    emitter_constants_.emit_request = emit_request;
    ++emitter_constants_.seed;
  }

  _Use_decl_annotations_
  void ParticleSystem::UpdateCB(_In_ const UINT frame_index)
  {
    EmitterConstants* data_begin{nullptr};
    CD3DX12_RANGE read_range(0, 0);
    COM_ERROR_IF_FAILED(emitter_constant_buffer_[frame_index]->Map(0, &read_range, reinterpret_cast<void**>(&data_begin)), "Failed to map the emitter constant buffer of the particle system.");
    data_begin->emit_request = emitter_constants_.emit_request;
    data_begin->seed         = emitter_constants_.seed;
    emitter_constant_buffer_[frame_index]->Unmap(0, nullptr);
  }

  _Use_decl_annotations_
  void ParticleSystem::Compute(ID3D12GraphicsCommandList* const command_list, const UINT frame_index)
  {
    ID3D12DescriptorHeap* pp_heaps[] = { cbv_srv_uav_heap_.Get() };
    command_list->SetDescriptorHeaps(_countof(pp_heaps), pp_heaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE particle_heap(cbv_srv_uav_heap_->GetGPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE particles_handle(particle_heap, ParticleHeap::kParticles, cbv_srv_uav_descriptor_size_);
    CD3DX12_GPU_DESCRIPTOR_HANDLE dead_list_handle(particle_heap, ParticleHeap::kDeadList, cbv_srv_uav_descriptor_size_);
    CD3DX12_GPU_DESCRIPTOR_HANDLE counters_handle(particle_heap,  ParticleHeap::kCounters, cbv_srv_uav_descriptor_size_);

    command_list->SetPipelineState(kickoff_cs_pipeline_state_.Get());
    command_list->SetComputeRootSignature(cs_root_signature_.Get());
    command_list->SetComputeRootConstantBufferView(0, emitter_constant_buffer_[frame_index]->GetGPUVirtualAddress());
    command_list->SetComputeRootDescriptorTable(1, particles_handle);
    command_list->SetComputeRootDescriptorTable(2, dead_list_handle);
    command_list->SetComputeRootDescriptorTable(3, counters_handle);

    // Update counter buffer.
    command_list->Dispatch(1, 1, 1);

    // Synchronize the counter buffer so the emitter reads the updated counters.
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(counter_.Get()));

    command_list->SetPipelineState(emitter_cs_pipeline_state_.Get());
    command_list->SetComputeRootSignature(cs_root_signature_.Get());
    command_list->SetComputeRootConstantBufferView(0, emitter_constant_buffer_[frame_index]->GetGPUVirtualAddress());
    command_list->SetComputeRootDescriptorTable(1, particles_handle);
    command_list->SetComputeRootDescriptorTable(2, dead_list_handle);
    command_list->SetComputeRootDescriptorTable(3, counters_handle);

    // Emit the particle(s).
    constexpr UINT thread_group_count_x = (kMaxParticles + 63) / 64;
    command_list->Dispatch(thread_group_count_x, 1, 1);
  }

  void ParticleSystem::DrawImGuiWindow()
  {
    ImGui::Begin("Particle System");
    if (ImGui::CollapsingHeader("Particle System"))
    {
      ImGui::Checkbox("Looping ", &system_state_.looping);
      ImGui::InputFloat("Duration", &system_state_.duration);
      ImGui::InputFloat("Delay   ", &system_state_.delay);
    }
    if (ImGui::CollapsingHeader("Emitter"))
    {
      ImGui::InputFloat("Rate over Time", &emitter_state_.rate_over_time);
    }

    ImGui::Separator();

    const char* button_text  = "Apply";
    const ImGuiStyle& style  = ImGui::GetStyle();
    const float window_width = ImGui::GetWindowSize().x;
    const float text_width   = ImGui::CalcTextSize(button_text).x;
    const float button_width = text_width + style.FramePadding.x * 2.f;
    ImGui::SetCursorPosX((window_width - button_width) * .5f);
    if (ImGui::Button(button_text))
    {
      // Update particle system.
    }

    ImGui::End();
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateConstantBuffers(_In_ ID3D12Device* const device)
  {
    static constexpr UINT emitter_constants_size = (sizeof(EmitterConstants) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

    for (std::ptrdiff_t i = 0; i < _countof(emitter_constant_buffer_); ++i)
    {
      COM_ERROR_IF_FAILED(device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(emitter_constants_size),
          D3D12_RESOURCE_STATE_GENERIC_READ,
          nullptr,
          IID_PPV_ARGS(&emitter_constant_buffer_[i])
        ),
        L"Failed to create a emitter constant buffer of the particle system."
      );
    }
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateLists(ID3D12Device* const device, ID3D12GraphicsCommandList* const command_list, ID3D12Resource* const particle_pool)
  {
    D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_uav_heap_desc {
      .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      .NumDescriptors = ParticleHeap::kCount,
      .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
      .NodeMask       = 0
    };

    COM_ERROR_IF_FAILED(device->CreateDescriptorHeap(&cbv_srv_uav_heap_desc, IID_PPV_ARGS(&cbv_srv_uav_heap_)), "Failed to create the particle system's descriptor heap.");
    cbv_srv_uav_descriptor_size_ = device->GetDescriptorHandleIncrementSize(cbv_srv_uav_heap_desc.Type);

    // Create the dead list and counter buffer.
    constexpr UINT64 dead_list_buffer_width = kMaxParticles * sizeof(uint32_t);
    constexpr UINT64 counter_buffer_width   = static_cast<UINT64>(CounterBuffer::kCount) * sizeof(uint32_t);
    {
      COM_ERROR_IF_FAILED(device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(dead_list_buffer_width, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
          D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,
          IID_PPV_ARGS(&dead_list_)
        ), 
        "Failed to create the dead list buffer of the particle system."
      );

      COM_ERROR_IF_FAILED(device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(counter_buffer_width, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
          D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,
          IID_PPV_ARGS(&counter_)
        ), 
        "Failed to create the counter buffer of the particle system."
      );
    }

    // Create the upload buffer and populate it, afterwards copy to the default buffers.
    {
      constexpr UINT64 buffer_width = dead_list_buffer_width + counter_buffer_width;

      COM_ERROR_IF_FAILED(device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(buffer_width),
          D3D12_RESOURCE_STATE_GENERIC_READ,
          nullptr,
          IID_PPV_ARGS(&upload_buffer_)
        ), 
        "Failed to create the upload buffer of the particle system."
      );

      uint32_t* data_begin{nullptr};
      CD3DX12_RANGE read_range(0, 0);
      COM_ERROR_IF_FAILED(upload_buffer_->Map(0, &read_range, reinterpret_cast<void**>(&data_begin)), "Failed to map the upload buffer of the particle system.");
      
      // Populate the upload buffer with the particle indices.
      for (uint32_t i = 0; i < kMaxParticles; ++i) data_begin[i] = i;

      // Shift pointer to the counter buffer region.
      uint32_t* counter_data = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(data_begin) + dead_list_buffer_width);

      // Populate the upload buffer with the length of each list.
      for (uint32_t i = 0; i < static_cast<uint32_t>(CounterBuffer::kCount); ++i) counter_data[i] = i == static_cast<uint32_t>(CounterBuffer::kDead) ? kMaxParticles : 0;
      
      upload_buffer_->Unmap(0, nullptr);

      // Copy the data from the upload buffer to the dead list buffer.
      command_list->CopyBufferRegion(dead_list_.Get(), 0, upload_buffer_.Get(), 0, dead_list_buffer_width);

      // Copy the data from the upload buffer to the counter buffer.
      command_list->CopyBufferRegion(counter_.Get(), 0, upload_buffer_.Get(), dead_list_buffer_width, counter_buffer_width);

      CD3DX12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(dead_list_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(counter_.Get(),   D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
      };

      command_list->ResourceBarrier(_countof(barriers), barriers);
    }

    // Create the uav views.
    {
      D3D12_UNORDERED_ACCESS_VIEW_DESC dead_list_uav_desc {
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer = {
          .FirstElement         = 0,
          .NumElements          = kMaxParticles,
          .StructureByteStride  = sizeof(uint32_t),
          .CounterOffsetInBytes = 0,
          .Flags                = D3D12_BUFFER_UAV_FLAG_NONE
        }
      };

      CD3DX12_CPU_DESCRIPTOR_HANDLE particle_heap(cbv_srv_uav_heap_->GetCPUDescriptorHandleForHeapStart());
      CD3DX12_CPU_DESCRIPTOR_HANDLE dead_list_handle(particle_heap, ParticleHeap::kDeadList, cbv_srv_uav_descriptor_size_);
      device->CreateUnorderedAccessView(dead_list_.Get(), nullptr, &dead_list_uav_desc, dead_list_handle);
      
      // The counter is a raw buffer to support indirect dispatching.
      D3D12_UNORDERED_ACCESS_VIEW_DESC counter_uav_desc {
        .Format        = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer = {
          .FirstElement         = 0,
          .NumElements          = static_cast<UINT>(CounterBuffer::kCount),
          .StructureByteStride  = 0,
          .CounterOffsetInBytes = 0,
          .Flags                = D3D12_BUFFER_UAV_FLAG_RAW
        }
      };

      CD3DX12_CPU_DESCRIPTOR_HANDLE counter_handle(particle_heap, ParticleHeap::kCounters, cbv_srv_uav_descriptor_size_);
      device->CreateUnorderedAccessView(counter_.Get(), nullptr, &counter_uav_desc, counter_handle);

      // Same as in D3D12HelloTriangle.cpp in the function LoadAssets().
      D3D12_UNORDERED_ACCESS_VIEW_DESC particles_uav_desc {
        .Format        = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer = {
          .FirstElement         = 0,
          .NumElements          = kMaxParticles,
          .StructureByteStride  = sizeof(Particle),
          .CounterOffsetInBytes = 0,
          .Flags                = D3D12_BUFFER_UAV_FLAG_NONE
        }
      };

      CD3DX12_CPU_DESCRIPTOR_HANDLE particles_handle(particle_heap, ParticleHeap::kParticles, cbv_srv_uav_descriptor_size_);
      device->CreateUnorderedAccessView(particle_pool, nullptr, &particles_uav_desc, particles_handle);
    }
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateSignature(ID3D12Device* const device)
  {
    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data {
      .HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1
    };

    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data))))
    {
      feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    D3D12_ROOT_SIGNATURE_FLAGS root_signature_flags =
      D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS        |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS          |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS        |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS      |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS         |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;

    // Define the particles, dead list and counter, respectively in slot u0: space0, 1 and 2.
    CD3DX12_DESCRIPTOR_RANGE1 uav_ranges[3];
    uav_ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
    uav_ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 1);
    uav_ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 2);

    CD3DX12_ROOT_PARAMETER1 root_parameters[4];
    root_parameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL); // slot b0.
    root_parameters[1].InitAsDescriptorTable(1, &uav_ranges[0], D3D12_SHADER_VISIBILITY_ALL);
    root_parameters[2].InitAsDescriptorTable(1, &uav_ranges[1], D3D12_SHADER_VISIBILITY_ALL);
    root_parameters[3].InitAsDescriptorTable(1, &uav_ranges[2], D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(_countof(root_parameters), root_parameters, 0, nullptr, root_signature_flags);

    ComPtr<ID3DBlob> signature{}, error{};
    COM_ERROR_IF_FAILED(D3DX12SerializeVersionedRootSignature(&root_signature_desc, feature_data.HighestVersion, &signature, &error), "Failed to serialize the root signature of the particle system.");
    COM_ERROR_IF_FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&cs_root_signature_)), "Failed to create the compute shader root signature of the particle system.");
  }

  _Use_decl_annotations_
  void ParticleSystem::CreatePipelineState(ID3D12Device* const device, ID3D12PipelineState** pipeline_state, const wchar_t* const compute_shader_path)
  {
    UINT8* shader_data{};
    UINT   shader_data_length{};

    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(compute_shader_path).c_str(), &shader_data, &shader_data_length), "Failed to read the compute shader of the particle system.");
   
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute_pso_desc {
      .pRootSignature = cs_root_signature_.Get(),
      .CS             = CD3DX12_SHADER_BYTECODE(shader_data, shader_data_length),
      .NodeMask       = 0,
      .CachedPSO      = { nullptr, 0 },
      .Flags          = D3D12_PIPELINE_STATE_FLAG_NONE
    };

    COM_ERROR_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(pipeline_state)), "Failed to create the compute shader of the particle system.");
    free(shader_data);
  }

  _Use_decl_annotations_
  void ParticleSystem::CreatePipelineStates(ID3D12Device* const device)
  {
    CreatePipelineState(device, &kickoff_cs_pipeline_state_, kKickoffComputeShaderPath);
    CreatePipelineState(device, &emitter_cs_pipeline_state_, kEmitterComputeShaderPath);
  }

}  // namespace patronus::effects