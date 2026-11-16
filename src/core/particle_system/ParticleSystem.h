#pragma once

#include "core/stdafx.h"
#include "core/COMException.h"
#include "core/DXSampleHelper.h"

namespace patronus::effects
{

  using Microsoft::WRL::ComPtr;

  struct ParticleSystemState
  {
    bool  looping{};
    float time{};        // Seconds since the emitter started emitting.
    float accumulator{};
    float duration{1.f}; // How long the emitter emits for in seconds.
    float delay{};       // How long the emitter waits before emitting, does NOT substract from duration.
  };

  struct EmitterState
  {
    float rate_over_time{}; // How many particles are emitted per second.
  };

  struct EmitterConstants
  {
    uint32_t emit_request{};
    uint32_t seed{};
  };

  struct SimulateConstants
  {
    float delta_time{};
  };

  struct Particle
  {
    DirectX::XMFLOAT3 pos{};
    DirectX::XMFLOAT3 vel{};
    float             lifetime{};
    float             _pad{};
  };

  class ParticleSystem
  {
    static constexpr UINT     kFramesInFlight{2};
    static constexpr uint32_t kMaxParticles{1'000'000};

    static constexpr const wchar_t* kKickoffComputeShaderPath{L"shaders_particle_system_kickoff_CS.cso"};
    static constexpr const wchar_t* kEmitterComputeShaderPath{L"shaders_particle_system_emitter_CS.cso"};
    static constexpr const wchar_t* kSimulateComputeShaderPath{L"shaders_particle_system_simulate_CS.cso"};
    static constexpr const wchar_t* kVertexShaderPath{L"shaders_particle_system_vertex_shader.cso"};
    static constexpr const wchar_t* kPixelShaderPath{L"shaders_particle_system_pixel_shader.cso"};

    // kC_... = Compute
    // kG_... = Graphics
    enum ParticleHeap : uint32_t 
    {
      kC_Particles_UAV = 0,
      kC_DeadList,
      kC_Counters,
      kG_Particles_SRV,
      kG_SoftParticles_DepthBuffer,
      kCG_AliveList_A_UAV,
      kCG_AliveList_A_SRV,
      kCG_AliveList_B_UAV,
      kCG_AliveList_B_SRV,
      kCG_Arguments,
      kCount
    };

    enum class CounterBuffer : uint32_t
    {
      kDead = 0,
      kEmit,
      kAliveIn,
      kAliveOut,
      kCount
    };

  public:
    ParticleSystem();

    void Init(_In_ ID3D12Device* const device, _In_ ID3D12GraphicsCommandList* const command_list, _In_ ID3D12Resource* const depth_buffer);
    void Resize(_In_ ID3D12Device* const device, _In_ ID3D12Resource* const depth_buffer);
    void ReleaseTemporaryBuffers();

    void CreateGraphicsPipelineState(_In_ ID3D12Device* const device,  _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC& standard_graphics_pso, _In_ const D3D12_DEPTH_STENCIL_DESC& standard_depth_stencil);

    void Update(_In_ const float delta_time);
    void UpdateCB(_In_ const UINT frame_index);
    void Compute(_In_ ID3D12GraphicsCommandList* const command_list, _In_ const UINT frame_index);
    void Render(_In_ ID3D12GraphicsCommandList* const command_list, _In_ ID3D12Resource* const camera);

    void DrawImGuiWindow();

  private:
    // Logic.
    ParticleSystemState system_state_;
    EmitterState        emitter_state_;
    EmitterConstants    emitter_constants_;
    SimulateConstants   simulate_constants_;

    // Rendering resources (TODO: ParticleSystem shouldn't handle this)
    UINT                           cbv_srv_uav_descriptor_size_;
    ComPtr<ID3D12DescriptorHeap>   cbv_srv_uav_heap_;

    uint8_t                        alive_list_order_;
    static constexpr uint32_t      alive_lists_heap_slots_[][2] = { 
      { ParticleHeap::kCG_AliveList_A_UAV, ParticleHeap::kCG_AliveList_A_SRV },
      { ParticleHeap::kCG_AliveList_B_UAV, ParticleHeap::kCG_AliveList_B_SRV }
    };

    ComPtr<ID3D12Resource>         upload_buffer_;
    ComPtr<ID3D12Resource>         emitter_constant_buffer_[kFramesInFlight];
    ComPtr<ID3D12Resource>         arguments_;                   // Holds the indirect arguments of the emit and simulate shaders and the instance count of the vertex shader.
    ComPtr<ID3D12Resource>         counter_;                     // Holds how much particles are dead, emitted or alive.
    ComPtr<ID3D12Resource>         dead_list_;                   // Holds the index of dead particles,  type is uint.
    ComPtr<ID3D12Resource>         alive_list_[2];               // Holds the index of alive particles, type is uint.
                                                                 // - IN:  all particles simulate processes.
                                                                 // - OUT: all particles alive after simulate.
    ComPtr<ID3D12Resource>         particle_pool_;               // Holds the particles their locations.

    ComPtr<ID3D12RootSignature>    cs_root_signature_;
    ComPtr<ID3D12RootSignature>    graphics_root_signature_;
    ComPtr<ID3D12PipelineState>    kickoff_cs_pipeline_state_;
    ComPtr<ID3D12PipelineState>    emitter_cs_pipeline_state_;
    ComPtr<ID3D12PipelineState>    simulate_cs_pipeline_state_;
    ComPtr<ID3D12PipelineState>    graphics_pipeline_state_;
    ComPtr<ID3D12CommandSignature> cs_command_signature_;
    //ComPtr<ID3D12CommandSignature>  graphics_command_signature_;

    void CreateConstantBuffers(_In_ ID3D12Device* const device);
    void CreateLists(_In_ ID3D12Device* const device, _In_ ID3D12GraphicsCommandList* const command_list);
    void CreateParticleBuffer(_In_ ID3D12Device* const device);
    void CreateDepthBufferView(_In_ ID3D12Device* const device, _In_ ID3D12Resource* const depth_buffer);
    void CreateArgumentsBuffer(_In_ ID3D12Device* const device);
    void CreateSignatures(_In_ ID3D12Device* const device);
    void CreateComputeSignature(_In_ ID3D12Device* const device, _In_ const D3D12_FEATURE_DATA_ROOT_SIGNATURE& feature_data);
    void CreateGraphicsSignature(_In_ ID3D12Device* const device, _In_ const D3D12_FEATURE_DATA_ROOT_SIGNATURE& feature_data);
    void CreateCommandSignature(_In_ ID3D12Device* const device);
    void CreateComputePipelineState(_In_ ID3D12Device* const device, _Out_ ID3D12PipelineState** pipeline_state, _In_ const wchar_t* const compute_shader_path);
    void CreateComputePipelineStates(_In_ ID3D12Device* const device);

    // TODO: use the one in DXSample class.
    std::wstring GetAssetFullPath(LPCWSTR assetName)
    {
      std::wstring string_assets_path;
      WCHAR assetsPath[512];
      GetAssetsPath(assetsPath, _countof(assetsPath));
      string_assets_path = assetsPath;

      return string_assets_path + assetName;
    }
  };

} // namespace patronus::effects