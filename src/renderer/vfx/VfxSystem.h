// The one class the application talks to for spell VFX.
//
// Skeleton written by Claude under a one-time, explicitly authorised
// exception to the CLAUDE.md hard rule (2026-09-18): structs, classes and
// empty functions with comments only. Bodies are written by the user.
//
// Per frame the app calls, in this order: Update (after its fence wait),
// RecordSimulate (before the opaque pass), RecordDraw (after the opaque
// pass, with the HDR target and read-only depth bound). Nothing else is
// needed to play both target spells; artists work on the JSON and in
// DrawDebugUi. Decision and layout: ADR-0010.
//
// Limits of this first version, on purpose: one active playback per loaded
// spell (Play restarts it), and spells are loaded one at a time by the app.

#pragma once

#include <D3D12MemAlloc.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgiformat.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "renderer/vfx/SpellAsset.h"
#include "renderer/vfx/SpellInstance.h"
#include "renderer/vfx/VfxPipeline.h"
#include "renderer/vfx/VfxResources.h"
#include "renderer/vfx/VfxTypes.h"

namespace patronus::vfx {

using SpellId = uint32_t;
inline constexpr SpellId kInvalidSpell = 0xFFFFFFFFu;

// What the app knows about the camera and the frame; copied into
// FrameConstants by Update. Kept as plain values so VfxSystem does not
// depend on Camera3D.
struct CameraView {
  DirectX::XMFLOAT4X4 view;
  DirectX::XMFLOAT4X4 proj;
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT3 right;
  DirectX::XMFLOAT3 up;
  float near_z;  // for the reverse-Z linearisation constants (ADR-0004)
  float far_z;   // 0 for an infinite far plane
  uint32_t width;
  uint32_t height;
  uint32_t scene_depth_srv;  // descriptor index of the scene depth SRV the app owns
};

class VfxSystem {
 public:
  struct InitDesc {
    ID3D12Device* device = nullptr;
    D3D12MA::Allocator* allocator = nullptr;
    uint32_t frames_in_flight = 0;
    DXGI_FORMAT hdr_format = DXGI_FORMAT_UNKNOWN;    // ADR-0003: R11G11B10_FLOAT
    DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN;  // ADR-0004: D32_FLOAT
    std::wstring asset_root;                         // deployed assets/ directory
    std::wstring shader_root;                        // where the .cso files are
    DescriptorHeapSlice descriptors;                 // this system's window into the app's heap
  };

  VfxSystem() = default;
  ~VfxSystem() = default;
  VfxSystem(const VfxSystem&) = delete;
  VfxSystem& operator=(const VfxSystem&) = delete;

  // Creates the pipeline and loads the shared textures (curl volume, noise
  // pack). The app must record RecordInitialUploads on a command list,
  // execute it, wait, then call OnInitialUploadsComplete before the first
  // frame.
  bool Initialize(const InitDesc& desc, std::string& out_error);

  // Releases everything; the app guarantees the GPU is idle.
  void Shutdown();

  // Parses the JSON, resolves curve rows, creates the spell's VfxResources.
  // Returns kInvalidSpell on failure with the reason in out_error. The
  // returned id stays valid across ReloadSpell.
  SpellId LoadSpell(const std::string& json_path, std::string& out_error);

  // Re-reads the JSON of a loaded spell. Re-creates the resources only when
  // a pool size changed (otherwise only the SpellDesc is replaced), restarts
  // the active playback. The app must have waited for the GPU when
  // resources are re-created; the function reports whether it did so.
  bool ReloadSpell(SpellId id, std::string& out_error);

  // Starts (or restarts) the spell's playback from origin toward target.
  void Play(SpellId id, DirectX::XMFLOAT3 origin, DirectX::XMFLOAT3 target);
  void Stop(SpellId id);
  bool IsPlaying(SpellId id) const;

  // Advances every playback and writes this frame's rows: FrameConstants
  // from camera, EmitterParams and RingParams from each SpellInstance, with
  // pool_first / pool_count filled from the spell's VfxResources. frame_index
  // selects the ring slot; the app calls this after the fence wait for that
  // slot and never again in the same frame.
  void Update(float dt, uint32_t frame_index, const CameraView& camera);

  // Records the one-time uploads of the shared textures and of every loaded
  // spell's resources. Also used after LoadSpell for a spell loaded later.
  void RecordInitialUploads(ID3D12GraphicsCommandList* command_list);
  void OnInitialUploadsComplete();

  // Compute passes for every playing spell (VfxPipeline::RecordSimulate).
  void RecordSimulate(ID3D12GraphicsCommandList* command_list);

  // Draw passes for every playing spell (VfxPipeline::RecordDraw). Called
  // with the HDR target and the read-only depth bound.
  void RecordDraw(ID3D12GraphicsCommandList* command_list);

  // ImGui: one collapsing header per loaded spell, one per emitter inside
  // it, every EmitterDesc and RingDesc field as a slider or checkbox, the
  // spawned total next to the pool size, Play / Stop / Restart / Reload /
  // Save buttons. Edits change the SpellDesc in memory and take effect the
  // next frame; Save writes them back with SaveSpell.
  void DrawDebugUi();

 private:
  struct LoadedSpell {
    std::string path;
    SpellDesc desc;
    VfxResources resources;
    std::unique_ptr<SpellInstance> instance;  // null when not playing
    uint32_t emitter_count = 0;               // particle emitters
    uint32_t ring_count_this_frame = 0;       // rows written by the last Update
    uint32_t ring_vertex_count = 0;           // max RingDesc::points * 2 over the spell
    bool uploads_pending = false;
  };

  // Fills FrameConstants for one spell: camera, timing, and every
  // descriptor index from resources_, shared_ and camera.scene_depth_srv.
  void WriteFrameConstants(LoadedSpell& spell, uint32_t frame_index, float dt, const CameraView& camera);

  InitDesc init_{};
  VfxPipeline pipeline_;
  SharedTextures shared_;
  Microsoft::WRL::ComPtr<ID3D12Resource>
      shared_staging_[2];                             // curl volume, noise pack; freed by OnInitialUploadsComplete
  std::vector<std::unique_ptr<LoadedSpell>> spells_;  // index is the SpellId
  uint32_t frame_index_ = 0;                          // ring slot written by the last Update
  uint32_t frame_counter_ = 0;    // monotonically increasing; (frame_counter_ & 1) picks the alive list
  float time_ = 0.0f;             // seconds since Initialize, for FrameConstants::time
  uint32_t next_descriptor_ = 0;  // sub-allocation cursor inside init_.descriptors
};

}  // namespace patronus::vfx
