// GPU memory of the VFX system: every buffer and texture, their descriptor
// indices, and the per-emitter ranges inside the shared pool buffers.
//
// Skeleton written by Claude under a one-time, explicitly authorised
// exception to the CLAUDE.md hard rule (2026-09-18): structs, classes and
// empty functions with comments only. No D3D12 calls here; the bodies that
// create resources, views and barriers are written by the user.
//
// One allocation per resource type, sub-ranged per emitter with fixed
// offsets, so one UAV barrier covers every emitter and the descriptor count
// does not grow with the emitter count (ADR-0010). Lifetime:
// created at VfxSystem::LoadSpell, released at Shutdown or reload after a
// WaitForGpu, never while a frame in flight may reference it (ADR-0002).

#pragma once

#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "renderer/vfx/SpellAsset.h"
#include "renderer/vfx/VfxTypes.h"

namespace patronus::vfx {

// A window into the application's shader-visible CBV/SRV/UAV heap that this
// system may fill. The app owns the heap; the system only writes views into
// [first, first + count) and reports the resulting indices for SM 6.6
// ResourceDescriptorHeap[] access.
struct DescriptorHeapSlice {
  ID3D12DescriptorHeap* heap = nullptr;
  uint32_t first = 0;
  uint32_t count = 0;
  uint32_t increment = 0;  // device->GetDescriptorHandleIncrementSize(CBV_SRV_UAV)
};

// Textures shared by every spell: loaded once by VfxSystem, referenced by
// index from every FrameConstants.
struct SharedTextures {
  Microsoft::WRL::ComPtr<ID3D12Resource> curl_volume;  // assets/noise/curl_noise_64_rgba16f (RGBA16F 3D, WRAP)
  Microsoft::WRL::ComPtr<ID3D12Resource> noise_pack;   // assets/noise/noise_pack_256_rgba8 (RGBA8 2D, WRAP, mips)
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> curl_volume_alloc;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> noise_pack_alloc;
  uint32_t curl_volume_srv = 0;
  uint32_t noise_pack_srv = 0;
};

// Where an emitter lives inside each shared pool buffer (particles, render
// particles, both alive lists, dead list all use the same range).
struct PoolRange {
  uint32_t first = 0;
  uint32_t count = 0;
};

class VfxResources {
 public:
  VfxResources() = default;
  ~VfxResources() = default;
  VfxResources(const VfxResources&) = delete;
  VfxResources& operator=(const VfxResources&) = delete;

  // Creates every buffer for the emitters of desc (pool sizes from
  // EmitterDesc::pool), the curve atlas texture named by desc.curve_atlas,
  // the upload ring buffers (frames_in_flight slots each), and views for all
  // of them in the slice. asset_root is the deployed assets/ directory.
  // Returns false with a message when the slice is too small or an asset
  // file is missing.
  bool Create(ID3D12Device* device, D3D12MA::Allocator* allocator, uint32_t frames_in_flight, const SpellDesc& desc,
              const std::wstring& asset_root, DescriptorHeapSlice slice, std::string& out_error);

  // Releases everything. Caller guarantees the GPU is idle.
  void Release();

  // Records the one-time uploads: the curve atlas texture copy and the
  // dead-list fill (slot i of every emitter's range gets index i, dead_count
  // = pool). Staging buffers are kept alive until ReleaseStaging.
  void RecordInitialUploads(ID3D12GraphicsCommandList* command_list);

  // Drops the staging buffers once the app has waited for the initial
  // uploads (roadmap M0: upload buffers are not kept for the lifetime of
  // the app).
  void ReleaseStaging();

  // Mapped CPU pointers into this frame's ring slot. Only valid to write
  // after the frame's fence wait (ADR-0002). Capacities are the row counts
  // the slot was created with.
  EmitterParams* EmitterParamsSlot(uint32_t frame_index);
  RingParams* RingParamsSlot(uint32_t frame_index);
  FrameConstants* FrameConstantsSlot(uint32_t frame_index);
  D3D12_GPU_VIRTUAL_ADDRESS FrameConstantsAddress(uint32_t frame_index) const;
  uint32_t max_emitters() const { return max_emitters_; }
  uint32_t max_rings() const { return max_rings_; }

  // Raw resources for VfxPipeline's barriers; indices for FrameConstants.
  ID3D12Resource* particles() const { return particles_.Get(); }
  ID3D12Resource* render_particles() const { return render_particles_.Get(); }
  ID3D12Resource* alive_list(uint32_t which) const { return alive_lists_[which & 1].Get(); }
  ID3D12Resource* dead_list() const { return dead_list_.Get(); }
  ID3D12Resource* counters() const { return counters_.Get(); }
  ID3D12Resource* indirect_args() const { return indirect_args_.Get(); }
  ID3D12Resource* quad_indices() const { return quad_indices_.Get(); }
  const D3D12_INDEX_BUFFER_VIEW& quad_index_view() const { return quad_index_view_; }
  const std::vector<PoolRange>& pool_ranges() const { return pool_ranges_; }

  // Descriptor indices this object created; VfxSystem copies them into
  // FrameConstants together with the SharedTextures indices.
  uint32_t particles_uav() const { return particles_uav_; }
  uint32_t render_particles_uav() const { return render_particles_uav_; }
  uint32_t render_particles_srv() const { return render_particles_srv_; }
  uint32_t alive_list_uav(uint32_t which) const { return alive_list_uav_[which & 1]; }
  uint32_t dead_list_uav() const { return dead_list_uav_; }
  uint32_t counters_uav() const { return counters_uav_; }
  uint32_t indirect_args_uav() const { return indirect_args_uav_; }
  uint32_t emitter_params_srv(uint32_t frame_index) const { return emitter_params_srv_[frame_index]; }
  uint32_t ring_params_srv(uint32_t frame_index) const { return ring_params_srv_[frame_index]; }
  uint32_t curve_atlas_srv() const { return curve_atlas_srv_; }

  // Loads a texture written by tools/vfxtools/rawtexture.py (<stem>.bin +
  // <stem>.json with width/height/depth/format/row_pitch) into a default
  // heap resource plus a staging buffer, and creates its SRV at the given
  // slot. Shared between the curve atlas here and SharedTextures in
  // VfxSystem. Returns false when the files are missing or the format is
  // not one the baker writes.
  static bool LoadRawTexture(ID3D12Device* device, D3D12MA::Allocator* allocator, const std::wstring& stem,
                             const DescriptorHeapSlice& slice, uint32_t slot,
                             Microsoft::WRL::ComPtr<ID3D12Resource>& out_texture,
                             Microsoft::WRL::ComPtr<D3D12MA::Allocation>& out_alloc,
                             Microsoft::WRL::ComPtr<ID3D12Resource>& out_staging, std::string& out_error);

 private:
  // Sizes derived from desc at Create.
  uint32_t frames_in_flight_ = 0;
  uint32_t max_emitters_ = 0;           // particle emitters in the spell
  uint32_t max_rings_ = 0;              // sum of RingDesc::count over ring emitters
  uint32_t total_slots_ = 0;            // sum of EmitterDesc::pool over particle emitters
  std::vector<PoolRange> pool_ranges_;  // one per particle emitter, in desc order

  // Default-heap buffers (ADR-0005). Each one spans all emitters.
  Microsoft::WRL::ComPtr<ID3D12Resource> particles_;         // Particle[total_slots]
  Microsoft::WRL::ComPtr<ID3D12Resource> render_particles_;  // RenderParticle[total_slots]
  Microsoft::WRL::ComPtr<ID3D12Resource> alive_lists_[2];    // uint[total_slots] each, ping-pong by binding
  Microsoft::WRL::ComPtr<ID3D12Resource> dead_list_;         // uint[total_slots], persistent
  Microsoft::WRL::ComPtr<ID3D12Resource> counters_;          // EmitterCounters[max_emitters], UAV state all frame
  Microsoft::WRL::ComPtr<ID3D12Resource> indirect_args_;     // EmitterIndirectArgs[max_emitters], own resource
  Microsoft::WRL::ComPtr<ID3D12Resource> quad_indices_;      // static 6 indices per quad for the largest pool
  Microsoft::WRL::ComPtr<ID3D12Resource> curve_atlas_;       // RGBA16F 2D, one row per curve, CLAMP
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> particles_alloc_;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> render_particles_alloc_;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> alive_lists_alloc_[2];
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> dead_list_alloc_;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> counters_alloc_;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> indirect_args_alloc_;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> quad_indices_alloc_;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> curve_atlas_alloc_;
  D3D12_INDEX_BUFFER_VIEW quad_index_view_{};

  // Upload-heap ring buffers, persistently mapped, frames_in_flight slots
  // each. Written by the CPU only inside the slot of the current frame.
  Microsoft::WRL::ComPtr<ID3D12Resource> emitter_params_upload_;   // EmitterParams[frames][max_emitters]
  Microsoft::WRL::ComPtr<ID3D12Resource> ring_params_upload_;      // RingParams[frames][max_rings]
  Microsoft::WRL::ComPtr<ID3D12Resource> frame_constants_upload_;  // FrameConstants[frames], 256-byte slots
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> emitter_params_alloc_;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> ring_params_alloc_;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> frame_constants_alloc_;
  EmitterParams* emitter_params_mapped_ = nullptr;
  RingParams* ring_params_mapped_ = nullptr;
  uint8_t* frame_constants_mapped_ = nullptr;

  // Staging for the one-time uploads; freed by ReleaseStaging.
  Microsoft::WRL::ComPtr<ID3D12Resource> dead_list_staging_;
  Microsoft::WRL::ComPtr<ID3D12Resource> quad_indices_staging_;
  Microsoft::WRL::ComPtr<ID3D12Resource> curve_atlas_staging_;

  // Descriptor indices (heap-relative, for ResourceDescriptorHeap[]).
  DescriptorHeapSlice slice_{};
  uint32_t particles_uav_ = 0;
  uint32_t render_particles_uav_ = 0;
  uint32_t render_particles_srv_ = 0;
  uint32_t alive_list_uav_[2] = {0, 0};
  uint32_t dead_list_uav_ = 0;
  uint32_t counters_uav_ = 0;
  uint32_t indirect_args_uav_ = 0;
  std::vector<uint32_t> emitter_params_srv_;  // one per frame slot
  std::vector<uint32_t> ring_params_srv_;     // one per frame slot
  uint32_t curve_atlas_srv_ = 0;
};

}  // namespace patronus::vfx
