// Skeleton written by Claude under a one-time, explicitly authorised
// exception to the CLAUDE.md hard rule (2026-09-18). Bodies are the user's.

#include "renderer/vfx/VfxResources.h"

namespace patronus::vfx {

bool VfxResources::Create(ID3D12Device* /*device*/, D3D12MA::Allocator* /*allocator*/, uint32_t /*frames_in_flight*/,
                          const SpellDesc& /*desc*/, const std::wstring& /*asset_root*/, DescriptorHeapSlice /*slice*/,
                          std::string& /*out_error*/) {
  // 1. Walk desc.emitters: particle emitters get a PoolRange {running total, pool}; ring emitters add
  //    RingDesc::count to max_rings_. total_slots_ is the sum of pools.
  // 2. Count the descriptors needed (7 UAV/SRV for the pools + 2 per frame slot + 1 curve atlas); fail if
  //    slice.count is smaller, naming both numbers.
  // 3. Default-heap buffers through the allocator, all in UNORDERED_ACCESS as their initial state except
  //    quad_indices_ (INDEX_BUFFER) and curve_atlas_ (COPY_DEST until the upload). ADR-0005 lists which
  //    ones change state during the frame.
  // 4. Upload-heap ring buffers, mapped once and never unmapped; FrameConstants slots are 256-byte aligned.
  // 5. Views: structured UAVs/SRVs for the typed buffers, raw (R32_TYPELESS, BUFFER_UAV_FLAG_RAW) for
  //    counters_ and indirect_args_, an SRV per frame slot for the params buffers, a 2D SRV for the atlas.
  // 6. LoadRawTexture(asset_root / desc.curve_atlas) for the curve atlas.
  // 7. Fill the CPU-side staging contents: dead lists (slot i within each range = i), quad indices
  //    (0,1,2, 2,1,3 per quad, offset by 4 per quad, for the largest pool).
  return false;
}

void VfxResources::Release() {
  // Unmap the upload buffers, reset every ComPtr in reverse creation order, clear the vectors.
}

void VfxResources::RecordInitialUploads(ID3D12GraphicsCommandList* /*command_list*/) {
  // CopyBufferRegion staging -> dead_list_, quad_indices_; CopyTextureRegion staging -> curve_atlas_;
  // then transitions: dead_list_ COPY_DEST -> UNORDERED_ACCESS, quad_indices_ -> INDEX_BUFFER,
  // curve_atlas_ -> ALL_SHADER_RESOURCE. Counters get an initial upload too (dead_count = pool,
  // everything else 0). Buffers created in UNORDERED_ACCESS need a transition to COPY_DEST first.
}

void VfxResources::ReleaseStaging() {
  // Reset the three staging ComPtrs. Only after the app's WaitForGpu following RecordInitialUploads.
}

EmitterParams* VfxResources::EmitterParamsSlot(uint32_t /*frame_index*/) {
  // emitter_params_mapped_ + frame_index * max_emitters_
  return nullptr;
}

RingParams* VfxResources::RingParamsSlot(uint32_t /*frame_index*/) {
  // ring_params_mapped_ + frame_index * max_rings_
  return nullptr;
}

FrameConstants* VfxResources::FrameConstantsSlot(uint32_t /*frame_index*/) {
  // reinterpret_cast<FrameConstants*>(frame_constants_mapped_ + frame_index * kSlotBytes) with kSlotBytes the
  // size of FrameConstants rounded up to 256.
  return nullptr;
}

D3D12_GPU_VIRTUAL_ADDRESS VfxResources::FrameConstantsAddress(uint32_t /*frame_index*/) const {
  // frame_constants_upload_->GetGPUVirtualAddress() + frame_index * kSlotBytes
  return 0;
}

bool VfxResources::LoadRawTexture(ID3D12Device* /*device*/, D3D12MA::Allocator* /*allocator*/,
                                  const std::wstring& /*stem*/, const DescriptorHeapSlice& /*slice*/, uint32_t /*slot*/,
                                  Microsoft::WRL::ComPtr<ID3D12Resource>& /*out_texture*/,
                                  Microsoft::WRL::ComPtr<D3D12MA::Allocation>& /*out_alloc*/,
                                  Microsoft::WRL::ComPtr<ID3D12Resource>& /*out_staging*/, std::string& /*out_error*/) {
  // 1. Read <stem>.json: width, height, depth, dxgi_format, row_pitch, slice_pitch, byte_size, bin.
  //    depth > 1 means a 3D texture (the curl volume); otherwise 2D.
  // 2. Read <stem>.bin; its size must equal byte_size.
  // 3. Create the default-heap texture (COPY_DEST) and an upload buffer sized by
  //    GetCopyableFootprints; memcpy row by row honouring the footprint's row pitch.
  // 4. Create the SRV at slice.first + slot (3D or 2D view to match).
  // The CopyTextureRegion and the transition happen in the caller's RecordInitialUploads.
  return false;
}

}  // namespace patronus::vfx
