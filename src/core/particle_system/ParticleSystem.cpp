#include "ParticleSystem.h"

namespace patronus::effects
{

  ParticleSystem::ParticleSystem()
    : system_state_{.duration = 10.f}
    , emitter_state_{.rate_over_time = 100'000.f}
    , emitter_constants_{}
    , simulate_constants_{}
    , cbv_srv_uav_descriptor_size_{}
    , cbv_srv_uav_heap_{}
    , alive_list_order_{}
    , particle_pool_{}
    , upload_buffer_{}
    , emitter_constant_buffer_{}
    , counter_{}
    , dead_list_{}
    , cs_root_signature_{}
    , kickoff_cs_pipeline_state_{}
    , emitter_cs_pipeline_state_{}
    , simulate_cs_pipeline_state_{}
    , graphics_pipeline_state_{}
  {
  }

  _Use_decl_annotations_
  void ParticleSystem::Init(ID3D12Device* const device, ID3D12GraphicsCommandList* const command_list, ID3D12Resource* const depth_buffer)
  {
    CreateConstantBuffers(device);
    CreateLists(device, command_list);
    CreateParticleBuffer(device);
    CreateDepthBufferView(device, depth_buffer);
    CreateArgumentsBuffer(device);
    CreateSignatures(device);
    CreateComputePipelineStates(device);
  }

  _Use_decl_annotations_
  void ParticleSystem::Resize(ID3D12Device* const device, ID3D12Resource* const depth_buffer)
  {
    CreateDepthBufferView(device, depth_buffer);
  }

  void ParticleSystem::ReleaseTemporaryBuffers()
  {
    upload_buffer_.Reset();
  }

  _Use_decl_annotations_
  void ParticleSystem::Update(const float delta_time)
  {
    simulate_constants_.delta_time =  delta_time;
    system_state_.time             += delta_time;

    uint32_t emit_request{};
    bool     active = system_state_.time >= system_state_.delay &&
                      system_state_.time <  system_state_.delay + system_state_.duration;

    if (active)
    {
      system_state_.accumulator += emitter_state_.rate_over_time * delta_time;
      emit_request              = static_cast<uint32_t>(system_state_.accumulator);
      system_state_.accumulator -= emit_request;
    }
    else if (system_state_.looping && system_state_.time >= system_state_.delay)
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
    CD3DX12_GPU_DESCRIPTOR_HANDLE particles_uav_handle(particle_heap, ParticleHeap::kC_Particles_UAV, cbv_srv_uav_descriptor_size_);
    CD3DX12_GPU_DESCRIPTOR_HANDLE dead_list_handle(particle_heap,     ParticleHeap::kC_DeadList,      cbv_srv_uav_descriptor_size_);
    CD3DX12_GPU_DESCRIPTOR_HANDLE counters_handle(particle_heap,      ParticleHeap::kC_Counters,      cbv_srv_uav_descriptor_size_);
    CD3DX12_GPU_DESCRIPTOR_HANDLE arguments_handle(particle_heap,     ParticleHeap::kCG_Arguments,    cbv_srv_uav_descriptor_size_);

    command_list->SetComputeRootSignature(cs_root_signature_.Get());
    command_list->SetComputeRootConstantBufferView(1, emitter_constant_buffer_[frame_index]->GetGPUVirtualAddress());
    command_list->SetComputeRootDescriptorTable(2, particles_uav_handle);
    command_list->SetComputeRootDescriptorTable(3, dead_list_handle);
    command_list->SetComputeRootDescriptorTable(4, counters_handle);
    command_list->SetComputeRootDescriptorTable(7, arguments_handle);

    // Update counter buffer.
    command_list->SetPipelineState(kickoff_cs_pipeline_state_.Get());
    command_list->Dispatch(1, 1, 1);

    // Synchronize the counter buffer so the emitter reads the updated counters.
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(counter_.Get()));

    // Indicate that the arguments buffer will be used as indirect arguments.
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(arguments_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));

    // Emit the particle(s).
    CD3DX12_GPU_DESCRIPTOR_HANDLE alive_list_write_emitted_particles(particle_heap, alive_lists_heap_slots_[alive_list_order_][0], cbv_srv_uav_descriptor_size_);
    
    command_list->SetComputeRootDescriptorTable(5, alive_list_write_emitted_particles);
    command_list->SetPipelineState(emitter_cs_pipeline_state_.Get());
    command_list->ExecuteIndirect(cs_command_signature_.Get(), 1u, arguments_.Get(), 0u, nullptr, 0u);

    // Synchronize the buffers so the displacement shader reads the updated states.
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(counter_.Get()));
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(dead_list_.Get()));
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(particle_pool_.Get()));

    // Simulate the particles.
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(alive_list_[alive_list_order_].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    CD3DX12_GPU_DESCRIPTOR_HANDLE alive_list_read_emitted_particles(particle_heap,    alive_lists_heap_slots_[alive_list_order_][1], cbv_srv_uav_descriptor_size_);
    alive_list_order_ = (alive_list_order_ + 1) & 0x01; // Switch between 0 and 1.
    CD3DX12_GPU_DESCRIPTOR_HANDLE alive_list_write_surviving_particles(particle_heap, alive_lists_heap_slots_[alive_list_order_][0], cbv_srv_uav_descriptor_size_);
    
    command_list->SetComputeRoot32BitConstants(0, 1, &simulate_constants_, 0);
    command_list->SetComputeRootDescriptorTable(5, alive_list_write_surviving_particles);
    command_list->SetComputeRootDescriptorTable(6, alive_list_read_emitted_particles);
    command_list->SetPipelineState(simulate_cs_pipeline_state_.Get());
    command_list->ExecuteIndirect(cs_command_signature_.Get(), 1u, arguments_.Get(), static_cast<UINT>(IndirectArguments::kX_SIM) * sizeof(uint32_t), nullptr, 0u);
    
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(alive_list_[(alive_list_order_ + 1) & 0x01].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
  }

  _Use_decl_annotations_
  void ParticleSystem::Render(ID3D12GraphicsCommandList* const command_list, ID3D12Resource* const camera)
  {
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(particle_pool_.Get(),                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(alive_list_[alive_list_order_].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
  
    // Copy the ALIVE_OUT count from the counters buffer to the arguments buffer InstanceCount.
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(counter_.Get(),   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,  D3D12_RESOURCE_STATE_COPY_SOURCE));
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(arguments_.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST));
    command_list->CopyBufferRegion(arguments_.Get(), static_cast<UINT>(IndirectArguments::kInstanceCount) * sizeof(uint32_t), 
                                   counter_.Get(),   static_cast<UINT>(CounterBuffer::kAliveOut)          * sizeof(uint32_t),
                                   sizeof(uint32_t));
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(counter_.Get(),   D3D12_RESOURCE_STATE_COPY_SOURCE,       D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(arguments_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,         D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));
    
    ID3D12DescriptorHeap* pp_heaps[] = { cbv_srv_uav_heap_.Get() };
    command_list->SetDescriptorHeaps(_countof(pp_heaps), pp_heaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE particle_heap(cbv_srv_uav_heap_->GetGPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE particles_handle(particle_heap,  ParticleHeap::kG_Particles_SRV,                cbv_srv_uav_descriptor_size_);
    CD3DX12_GPU_DESCRIPTOR_HANDLE depth_handle(particle_heap,      ParticleHeap::kG_SoftParticles_DepthBuffer,    cbv_srv_uav_descriptor_size_);
    CD3DX12_GPU_DESCRIPTOR_HANDLE alive_list_handle(particle_heap, alive_lists_heap_slots_[alive_list_order_][1], cbv_srv_uav_descriptor_size_);
    CD3DX12_GPU_DESCRIPTOR_HANDLE counters_handle(particle_heap,   ParticleHeap::kC_Counters,                     cbv_srv_uav_descriptor_size_);

    command_list->SetPipelineState(graphics_pipeline_state_.Get());
    command_list->SetGraphicsRootSignature(graphics_root_signature_.Get());
    command_list->SetGraphicsRootConstantBufferView(0, camera->GetGPUVirtualAddress());
    command_list->SetGraphicsRootDescriptorTable(1, particles_handle);
    command_list->SetGraphicsRootDescriptorTable(2, depth_handle);
    command_list->SetGraphicsRootDescriptorTable(3, alive_list_handle);
    command_list->SetGraphicsRootDescriptorTable(4, counters_handle);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    command_list->ExecuteIndirect(graphics_command_signature_.Get(), 1u, arguments_.Get(), static_cast<UINT>(IndirectArguments::kVertexCountPerInstance) * sizeof(uint32_t), nullptr, 0u);

    // Indicate that the arguments buffer will be used as indirect arguments.
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(arguments_.Get(),                      D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,         D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(alive_list_[alive_list_order_].Get(),  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(particle_pool_.Get(),                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
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

    for (size_t i = 0; i < _countof(emitter_constant_buffer_); ++i)
    {
      COM_ERROR_IF_FAILED(device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(emitter_constants_size),
          D3D12_RESOURCE_STATE_GENERIC_READ,
          nullptr,
          IID_PPV_ARGS(&emitter_constant_buffer_[i])
        ),
        L"Failed to create an emitter constant buffer of the particle system."
      );
    }
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateLists(ID3D12Device* const device, ID3D12GraphicsCommandList* const command_list)
  {
    D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_uav_heap_desc {
      .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      .NumDescriptors = ParticleHeap::kCount,
      .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
      .NodeMask       = 0
    };

    COM_ERROR_IF_FAILED(device->CreateDescriptorHeap(&cbv_srv_uav_heap_desc, IID_PPV_ARGS(&cbv_srv_uav_heap_)), "Failed to create the particle system's descriptor heap.");
    cbv_srv_uav_descriptor_size_ = device->GetDescriptorHandleIncrementSize(cbv_srv_uav_heap_desc.Type);

    // Create the dead list, counter buffer and alive lists.
    constexpr UINT64 dead_list_buffer_width  = kMaxParticles * sizeof(uint32_t);
    constexpr UINT64 alive_list_buffer_width = dead_list_buffer_width;
    constexpr UINT64 counter_buffer_width    = static_cast<UINT64>(CounterBuffer::kCount) * sizeof(uint32_t);
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

      // Alive lists don't need to be filled at the start since at start there are no particles alive.
      for (size_t i = 0; i < _countof(alive_list_); ++i)
      {
        COM_ERROR_IF_FAILED(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(alive_list_buffer_width, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, //
            nullptr,
            IID_PPV_ARGS(&alive_list_[i])
          ), 
          "Failed to create alive list " + std::to_string(i) + " buffer of the particle system."
        );
      }
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
      CD3DX12_CPU_DESCRIPTOR_HANDLE dead_list_handle(particle_heap, ParticleHeap::kC_DeadList, cbv_srv_uav_descriptor_size_);
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

      CD3DX12_CPU_DESCRIPTOR_HANDLE counter_handle(particle_heap, ParticleHeap::kC_Counters, cbv_srv_uav_descriptor_size_);
      device->CreateUnorderedAccessView(counter_.Get(), nullptr, &counter_uav_desc, counter_handle);

      D3D12_UNORDERED_ACCESS_VIEW_DESC alive_list_uav_desc(dead_list_uav_desc);
      D3D12_SHADER_RESOURCE_VIEW_DESC  alive_list_srv_desc {
        .Format                  = DXGI_FORMAT_UNKNOWN,
        .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {
          .FirstElement        = 0,
          .NumElements         = kMaxParticles,
          .StructureByteStride = sizeof(uint32_t),
          .Flags               = D3D12_BUFFER_SRV_FLAG_NONE
        }
      };

      for (size_t i = 0; i < _countof(alive_list_); ++i)
      {
        CD3DX12_CPU_DESCRIPTOR_HANDLE alive_list_A_uav_handle(particle_heap, alive_lists_heap_slots_[i][0], cbv_srv_uav_descriptor_size_);
        CD3DX12_CPU_DESCRIPTOR_HANDLE alive_list_A_srv_handle(particle_heap, alive_lists_heap_slots_[i][1], cbv_srv_uav_descriptor_size_);
        device->CreateUnorderedAccessView(alive_list_[i].Get(), nullptr, &alive_list_uav_desc, alive_list_A_uav_handle);
        device->CreateShaderResourceView(alive_list_[i].Get(), &alive_list_srv_desc, alive_list_A_srv_handle);
      }
    }
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateParticleBuffer(ID3D12Device* const device)
  {
    // Create the structured particle buffer.
    {
      const UINT particle_buffer_size = sizeof(Particle) * kMaxParticles;

      COM_ERROR_IF_FAILED(device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(particle_buffer_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          nullptr,
          IID_PPV_ARGS(&particle_pool_)
        ), 
        "Failed to create the structured particle buffer."
      );
    }

    // Create the uav and srv view.
    {
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

      D3D12_SHADER_RESOURCE_VIEW_DESC particles_srv_desc {
        .Format                  = DXGI_FORMAT_UNKNOWN,
        .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {
          .FirstElement        = 0,
          .NumElements         = kMaxParticles,
          .StructureByteStride = sizeof(Particle),
          .Flags               = D3D12_BUFFER_SRV_FLAG_NONE
        }
      };

      CD3DX12_CPU_DESCRIPTOR_HANDLE particle_heap(cbv_srv_uav_heap_->GetCPUDescriptorHandleForHeapStart());
      CD3DX12_CPU_DESCRIPTOR_HANDLE uav_handle(particle_heap, ParticleHeap::kC_Particles_UAV, cbv_srv_uav_descriptor_size_);
      CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(particle_heap, ParticleHeap::kG_Particles_SRV, cbv_srv_uav_descriptor_size_);
      
      device->CreateUnorderedAccessView(particle_pool_.Get(), nullptr, &particles_uav_desc, uav_handle);
      device->CreateShaderResourceView(particle_pool_.Get(), &particles_srv_desc, srv_handle);
    }
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateDepthBufferView(ID3D12Device* const device, ID3D12Resource* const depth_buffer)
  {
    constexpr DXGI_FORMAT shader_resource_format = DXGI_FORMAT_R32_FLOAT;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_description {
      .Format                  = shader_resource_format,
      .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
      .Texture2D = {
        .MostDetailedMip     = 0u,
        .MipLevels           = 1u,
        .PlaneSlice          = 0u,
        .ResourceMinLODClamp = 0.f
      }
    };

    CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(cbv_srv_uav_heap_->GetCPUDescriptorHandleForHeapStart(), ParticleHeap::kG_SoftParticles_DepthBuffer, cbv_srv_uav_descriptor_size_);
    device->CreateShaderResourceView(depth_buffer, &srv_description, srv_handle);
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateArgumentsBuffer(ID3D12Device* const device)
  {
    // Create the raw arguments buffer.
    constexpr UINT arguments_count = static_cast<UINT>(IndirectArguments::kCount);
    {
      const UINT arguments_buffer_size = sizeof(uint32_t) * arguments_count;

      COM_ERROR_IF_FAILED(device->CreateCommittedResource(
          &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
          D3D12_HEAP_FLAG_NONE,
          &CD3DX12_RESOURCE_DESC::Buffer(arguments_buffer_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          nullptr,
          IID_PPV_ARGS(&arguments_)
        ), 
        "Failed to create the arguments buffer of the particle system."
      );
    }

    // Create the uav view.
    {
      D3D12_UNORDERED_ACCESS_VIEW_DESC arguments_uav_desc {
        .Format        = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer = {
          .FirstElement         = 0u,
          .NumElements          = arguments_count,
          .StructureByteStride  = 0u,
          .CounterOffsetInBytes = 0u,
          .Flags                = D3D12_BUFFER_UAV_FLAG_RAW
        }
      };

      CD3DX12_CPU_DESCRIPTOR_HANDLE particle_heap(cbv_srv_uav_heap_->GetCPUDescriptorHandleForHeapStart());
      CD3DX12_CPU_DESCRIPTOR_HANDLE uav_handle(particle_heap, ParticleHeap::kCG_Arguments, cbv_srv_uav_descriptor_size_);
      device->CreateUnorderedAccessView(arguments_.Get(), nullptr, &arguments_uav_desc, uav_handle);
    }
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateSignatures(ID3D12Device* const device)
  {
    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data {
      .HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1
    };

    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data))))
    {
      feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    CreateComputeSignature(device, feature_data);
    CreateGraphicsSignature(device, feature_data);
    CreateComputeCommandSignatures(device);
    CreateGraphicsCommandSignatures(device);
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateComputeSignature(ID3D12Device* const device, const D3D12_FEATURE_DATA_ROOT_SIGNATURE& feature_data)
  {
    D3D12_ROOT_SIGNATURE_FLAGS root_signature_flags =
      D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS        |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS          |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS        |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS      |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS         |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;

    // Define the particles, dead list and counter, respectively in slot u0: space0, 1 and 2.
    CD3DX12_DESCRIPTOR_RANGE1 uav_ranges[5];
    uav_ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0); // Particles.
    uav_ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 1); // DeadList.
    uav_ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 2); // Counters.
    uav_ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 3); // AliveList_OUT.
    uav_ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 4); // Arguments.

    CD3DX12_DESCRIPTOR_RANGE1 srv_ranges;
    srv_ranges.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);     // AliveList_IN

    CD3DX12_ROOT_PARAMETER1 root_parameters[8];
    root_parameters[0].InitAsConstants(1, 0, 0, D3D12_SHADER_VISIBILITY_ALL);                                        // Simulation constants: slot b0.
    root_parameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL); // Emitter constants:    slot b1.
    root_parameters[2].InitAsDescriptorTable(1, &uav_ranges[0], D3D12_SHADER_VISIBILITY_ALL);
    root_parameters[3].InitAsDescriptorTable(1, &uav_ranges[1], D3D12_SHADER_VISIBILITY_ALL);
    root_parameters[4].InitAsDescriptorTable(1, &uav_ranges[2], D3D12_SHADER_VISIBILITY_ALL);
    root_parameters[5].InitAsDescriptorTable(1, &uav_ranges[3], D3D12_SHADER_VISIBILITY_ALL);
    root_parameters[6].InitAsDescriptorTable(1, &srv_ranges,    D3D12_SHADER_VISIBILITY_ALL);
    root_parameters[7].InitAsDescriptorTable(1, &uav_ranges[4], D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(_countof(root_parameters), root_parameters, 0, nullptr, root_signature_flags);

    ComPtr<ID3DBlob> signature{}, error{};
    COM_ERROR_IF_FAILED(D3DX12SerializeVersionedRootSignature(&root_signature_desc, feature_data.HighestVersion, &signature, &error), "Failed to serialize the compute root signature of the particle system.");
    COM_ERROR_IF_FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&cs_root_signature_)), "Failed to create the compute shader root signature of the particle system.");
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateGraphicsSignature(ID3D12Device* const device, const D3D12_FEATURE_DATA_ROOT_SIGNATURE& feature_data)
  {
    D3D12_ROOT_SIGNATURE_FLAGS root_signature_flags =
      D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS          |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS        |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS      |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;

    // Define the particle structured buffer and the depth buffer.
    CD3DX12_DESCRIPTOR_RANGE1 srv_ranges[3];
    srv_ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
    srv_ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 1);
    srv_ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 2);

    CD3DX12_DESCRIPTOR_RANGE1 uav_ranges;
    uav_ranges.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

    CD3DX12_ROOT_PARAMETER1 root_parameters[5];
    root_parameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL); // slot b0.
    root_parameters[1].InitAsDescriptorTable(1, &srv_ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);
    root_parameters[2].InitAsDescriptorTable(1, &srv_ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
    root_parameters[3].InitAsDescriptorTable(1, &srv_ranges[2], D3D12_SHADER_VISIBILITY_VERTEX);
    root_parameters[4].InitAsDescriptorTable(1, &uav_ranges,    D3D12_SHADER_VISIBILITY_VERTEX);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(_countof(root_parameters), root_parameters, 0, nullptr, root_signature_flags);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    COM_ERROR_IF_FAILED(D3DX12SerializeVersionedRootSignature(&root_signature_desc, feature_data.HighestVersion, &signature, &error), "Failed to serialize the graphics root signature of the particle system.");
    COM_ERROR_IF_FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&graphics_root_signature_)), "Failed to create the graphics root signature of the particle system.");
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateComputeCommandSignatures(ID3D12Device* const device)
  {
    D3D12_INDIRECT_ARGUMENT_DESC dispatch_desc {
      .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH
    };

    D3D12_INDIRECT_ARGUMENT_DESC arguments[] = {
      dispatch_desc
    };

    D3D12_COMMAND_SIGNATURE_DESC command_desc {
      .ByteStride       = sizeof(D3D12_DISPATCH_ARGUMENTS),
      .NumArgumentDescs = _countof(arguments),
      .pArgumentDescs   = arguments,
      .NodeMask         = 0u
    };
    
    COM_ERROR_IF_FAILED(device->CreateCommandSignature(&command_desc, nullptr, IID_PPV_ARGS(&cs_command_signature_)), "Failed to create the compute command signature of the particle system.");
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateGraphicsCommandSignatures(ID3D12Device* const device)
  {
    D3D12_INDIRECT_ARGUMENT_DESC draw_desc {
      .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW
    };

    D3D12_INDIRECT_ARGUMENT_DESC arguments[] = {
      draw_desc
    };

    D3D12_COMMAND_SIGNATURE_DESC command_desc {
      .ByteStride       = sizeof(D3D12_DRAW_ARGUMENTS),
      .NumArgumentDescs = _countof(arguments),
      .pArgumentDescs   = arguments,
      .NodeMask         = 0u
    };
    
    COM_ERROR_IF_FAILED(device->CreateCommandSignature(&command_desc, nullptr, IID_PPV_ARGS(&graphics_command_signature_)), "Failed to create the graphics command signature of the particle system.");
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateComputePipelineState(ID3D12Device* const device, ID3D12PipelineState** pipeline_state, const wchar_t* const compute_shader_path)
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

    COM_ERROR_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(pipeline_state)), "Failed to create a compute shader of the particle system.");
    free(shader_data);
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateComputePipelineStates(ID3D12Device* const device)
  {
    CreateComputePipelineState(device, &kickoff_cs_pipeline_state_,  kKickoffComputeShaderPath);
    CreateComputePipelineState(device, &emitter_cs_pipeline_state_,  kEmitterComputeShaderPath);
    CreateComputePipelineState(device, &simulate_cs_pipeline_state_, kSimulateComputeShaderPath);
  }

  _Use_decl_annotations_
  void ParticleSystem::CreateGraphicsPipelineState(ID3D12Device* const device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& standard_graphics_pso, const D3D12_DEPTH_STENCIL_DESC& standard_depth_stencil)
  {
    UINT8* vertex_shader_data{};
    UINT8* pixel_shader_data{};
    UINT   vertex_shader_data_length{}, pixel_shader_data_length{};

    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(kVertexShaderPath).c_str(), &vertex_shader_data, &vertex_shader_data_length), "Failed to read the vertex shader of the particle system.");
    COM_ERROR_IF_FAILED(ReadDataFromFile(GetAssetFullPath(kPixelShaderPath).c_str(),  &pixel_shader_data,  &pixel_shader_data_length),  "Failed to read the pixel shader of the particle system.");

    // To make soft particles possible the depth is used as an SRV, so the resource has to be read only.
    D3D12_DEPTH_STENCIL_DESC depth_state_particles(standard_depth_stencil);
    depth_state_particles.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_pso_desc(standard_graphics_pso);
    graphics_pso_desc.pRootSignature    = graphics_root_signature_.Get();
    graphics_pso_desc.InputLayout       = { nullptr, 0 };
    graphics_pso_desc.VS                = CD3DX12_SHADER_BYTECODE(vertex_shader_data, vertex_shader_data_length);
    graphics_pso_desc.PS                = CD3DX12_SHADER_BYTECODE(pixel_shader_data,  pixel_shader_data_length);
    graphics_pso_desc.DepthStencilState = depth_state_particles;

    COM_ERROR_IF_FAILED(device->CreateGraphicsPipelineState(&graphics_pso_desc, IID_PPV_ARGS(&graphics_pipeline_state_)), "Failed to create the particle graphics pipeline state.");
    free(vertex_shader_data);
    free(pixel_shader_data);
  }

}  // namespace patronus::effects