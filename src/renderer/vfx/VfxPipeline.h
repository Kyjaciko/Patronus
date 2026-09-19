// Pipeline objects of the VFX system and the recording of its passes.
//
// Skeleton written by Claude under a one-time, explicitly authorised
// exception to the CLAUDE.md hard rule (2026-09-18): structs, classes and
// empty functions with comments only. No D3D12 calls here; root signature,
// PSOs, command signatures, barriers and the pass order are written by the
// user following ADR-0005 and ADR-0006.
//
// Five PSOs, one root signature, two command signatures, and two Record*
// functions. Nothing here knows what a spell is; it only knows the buffers
// in VfxResources and the counts VfxSystem passes in.

#pragma once

#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

#include "renderer/vfx/VfxResources.h"

namespace patronus::vfx {

class VfxPipeline {
 public:
  VfxPipeline() = default;
  ~VfxPipeline() = default;
  VfxPipeline(const VfxPipeline&) = delete;
  VfxPipeline& operator=(const VfxPipeline&) = delete;

  // Creates the root signature (root CBV for FrameConstants, root constants
  // for RootConstants below, flag CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED, no
  // tables), the five PSOs from the .cso files in shader_dir (kickoff,
  // spawn, simulate; particle VS/PS and ring VS/PS with premultiplied blend
  // ONE / INV_SRC_ALPHA, depth GREATER, no depth write, render target
  // hdr_format, depth depth_format), and the two command signatures
  // (DISPATCH; CONSTANT + DRAW_INDEXED, stride sizeof(EmitterIndirectArgs)).
  bool Create(ID3D12Device* device, DXGI_FORMAT hdr_format, DXGI_FORMAT depth_format, const std::wstring& shader_dir,
              std::string& out_error);

  void Release();

  // Records kickoff, spawn and simulate for one loaded spell:
  //   set compute root signature, root CBV = res.FrameConstantsAddress(frame_index)
  //   Kickoff  Dispatch(ceil(emitter_count / 64), 1, 1)
  //   UAV barrier: counters, indirect_args
  //   Spawn    ExecuteIndirect(dispatch signature, emitter_count, indirect_args, offset of spawn_groups_x)
  //   UAV barrier: particles, alive lists, dead list, counters
  //   Simulate ExecuteIndirect(dispatch signature, emitter_count, indirect_args, offset of sim_groups_x)
  //   UAV barrier: render_particles, counters, indirect_args
  //   transitions: indirect_args UAV -> INDIRECT_ARGUMENT, render_particles UAV -> ALL_SHADER_RESOURCE
  // The indirect_args stride is sizeof(EmitterIndirectArgs), so one buffer
  // serves both ExecuteIndirect calls with different argument offsets.
  void RecordSimulate(ID3D12GraphicsCommandList* command_list, const VfxResources& res, uint32_t frame_index,
                      uint32_t emitter_count);

  // Records the particle draw and the ring draw into the bound HDR target
  // with the read-only depth (ADR-0004; the app binds both):
  //   set graphics root signature, root CBV, index buffer = quad indices, topology triangle list
  //   Particles ExecuteIndirect(draw signature, emitter_count, indirect_args, offset of emitter_index)
  //   Rings     DrawInstanced(max ring points * 2, ring_count) with the ring PSO, when ring_count > 0
  //   transitions back: indirect_args -> UNORDERED_ACCESS, render_particles -> UNORDERED_ACCESS
  void RecordDraw(ID3D12GraphicsCommandList* command_list, const VfxResources& res, uint32_t frame_index,
                  uint32_t emitter_count, uint32_t ring_count, uint32_t ring_vertex_count);

 private:
  // Root parameter slots shared by the compute and graphics stages.
  enum RootParam : uint32_t {
    kFrameConstantsCbv = 0,  // root CBV: FrameConstants of this frame
    kRootConstants = 1,      // 32-bit constants: see RootConstants
    kRootParamCount
  };

  // Layout of the root constants. emitter_index is the one written by the
  // CONSTANT indirect argument for the particle draw; the compute kernels
  // read the emitter index from their dispatch instead (the kickoff kernel
  // from the thread id, spawn and simulate from the CONSTANT argument once
  // the dispatch signature carries one, or from a per-emitter offset).
  struct RootConstants {
    uint32_t frame_slot;     // which ring slot the params SRVs point to
    uint32_t emitter_index;  // filled by ExecuteIndirect for the draw
    uint32_t alive_current;  // 0 or 1: which alive list is current this frame
    uint32_t pad0;
  };

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> kickoff_pso_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> spawn_pso_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> simulate_pso_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> particle_pso_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> ring_pso_;
  Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatch_signature_;  // DISPATCH
  Microsoft::WRL::ComPtr<ID3D12CommandSignature> draw_signature_;      // CONSTANT (emitter_index) + DRAW_INDEXED
};

}  // namespace patronus::vfx
