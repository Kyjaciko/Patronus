// Skeleton written by Claude under a one-time, explicitly authorised
// exception to the CLAUDE.md hard rule (2026-09-18). Bodies are the user's.

#include "renderer/vfx/VfxPipeline.h"

namespace patronus::vfx {

bool VfxPipeline::Create(ID3D12Device* /*device*/, DXGI_FORMAT /*hdr_format*/, DXGI_FORMAT /*depth_format*/,
                         const std::wstring& /*shader_dir*/, std::string& /*out_error*/) {
  // 1. Root signature 1.1: [0] root CBV b0, [1] 32-bit constants b1 (sizeof(RootConstants) / 4),
  //    flags CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED (static samplers are also fine:
  //    WRAP trilinear for the curl volume and noise pack, CLAMP linear for the curve atlas, POINT for depth).
  //    No DENY flags for the pixel stage: the PS reads the depth SRV and the noise pack.
  // 2. Load the six .cso files from shader_dir (names from CMakeLists.txt: vfx_Kickoff, vfx_Spawn,
  //    vfx_Simulate, vfx_ParticleVS, vfx_ParticlePS, vfx_RingVS, vfx_RingPS).
  // 3. Three compute PSOs.
  // 4. Two graphics PSOs: no input layout, triangle list, blend ONE / INV_SRC_ALPHA (ADR-0006), depth test
  //    GREATER with write mask ZERO (ADR-0004), RTV hdr_format, DSV depth_format, cull none.
  // 5. Command signatures: {DISPATCH} with stride sizeof(EmitterIndirectArgs) and null root signature;
  //    {CONSTANT (RootParam::kRootConstants, dest offset of emitter_index, 1 value), DRAW_INDEXED} with the
  //    same stride and this root signature (required when a CONSTANT argument is present).
  return false;
}

void VfxPipeline::Release() {
  // Reset every ComPtr.
}

void VfxPipeline::RecordSimulate(ID3D12GraphicsCommandList* /*command_list*/, const VfxResources& /*res*/,
                                 uint32_t /*frame_index*/, uint32_t /*emitter_count*/) {
  // Order and barriers exactly as listed in the header comment and ADR-0005. Every dispatch-to-dispatch
  // dependency on the same UAV gets a UAV barrier even when the debug layer is silent about it.
}

void VfxPipeline::RecordDraw(ID3D12GraphicsCommandList* /*command_list*/, const VfxResources& /*res*/,
                             uint32_t /*frame_index*/, uint32_t /*emitter_count*/, uint32_t /*ring_count*/,
                             uint32_t /*ring_vertex_count*/) {
  // Particle ExecuteIndirect, ring DrawInstanced, transitions back. The app has already bound the HDR RTV and
  // the read-only DSV and transitioned depth to DEPTH_READ | PIXEL_SHADER_RESOURCE.
}

}  // namespace patronus::vfx
