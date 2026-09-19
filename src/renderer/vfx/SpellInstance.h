// Playback of one spell: the timeline, the spawn accounting, and the rows
// the GPU reads this frame.
//
// Skeleton written by Claude under a one-time, explicitly authorised
// exception to the CLAUDE.md hard rule (2026-09-18): structs, classes and
// empty functions with comments only. Bodies are written by the user.
//
// Pure CPU, no D3D12, so it is unit-testable: total spawns over a run,
// bursts firing exactly once, anchor positions at the timeline knots. This
// is the C++ twin of previs.Timeline plus the per-emitter accumulator in
// previs.Emitter.step. It never touches GPU memory itself: VfxSystem hands
// it the mapped ring-slot pointers.

#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

#include "renderer/vfx/SpellAsset.h"
#include "renderer/vfx/VfxTypes.h"

namespace patronus::vfx {

class SpellInstance {
 public:
  // desc must outlive the instance (VfxSystem owns both). origin and target
  // override the JSON timeline knots so the same spell can be cast anywhere;
  // seed varies the per-frame hash so two casts do not look identical.
  SpellInstance(const SpellDesc* desc, DirectX::XMFLOAT3 origin, DirectX::XMFLOAT3 target, uint32_t seed);

  // Back to t = 0 with all accumulators cleared; used by the debug UI and
  // by hot reload.
  void Restart();

  // Advances spell time by dt (already clamped by the app) and computes,
  // per emitter, this frame's spawn count: floor(rate * dt + carry) inside
  // the active window plus every burst whose time was crossed during this
  // step. Rings are born when their burst time is crossed. Must be called
  // exactly once per frame before the Write* calls.
  void Advance(float dt);

  // True once time >= desc->end and no particle can still be alive
  // (end + longest life_max), so VfxSystem can drop the instance.
  bool IsFinished() const;

  float time() const { return time_; }

  // Writes one EmitterParams row per particle emitter, in desc order, into
  // out (capacity rows available). Fills the per-frame fields (anchor_pos,
  // anchor_vel, spawn_count, time, follow_anchor, frame_seed) on top of the
  // static params copied from EmitterDesc. Leaves pool_first / pool_count
  // zero: VfxSystem fills those from VfxResources. Returns rows written.
  uint32_t WriteEmitterParams(EmitterParams* out, uint32_t capacity) const;

  // Writes one RingParams row per live ring copy (RingDesc::count copies
  // per ring emitter) into out. Returns rows written; 0 when no ring is
  // alive, in which case VfxSystem skips the ring draw entirely.
  uint32_t WriteRingParams(RingParams* out, uint32_t capacity) const;

  // Timeline: origin until charge_end, linear to target until impact, then
  // target. kImpact resolves to target, kOrigin to origin. Same rule as
  // previs.Timeline.anchor so the previs and the runtime agree.
  DirectX::XMFLOAT3 AnchorPosition(Attach attach, float t) const;

  // Finite difference of AnchorPosition over the last step; zero for
  // kOrigin and kImpact and outside the flight window.
  DirectX::XMFLOAT3 AnchorVelocity(Attach attach, float t) const;

  // Debug UI: spawns issued so far per emitter, to compare with the pool
  // size and the previs report.
  uint32_t SpawnedTotal(uint32_t emitter_index) const;

 private:
  struct EmitterState {
    float spawn_carry = 0.0f;       // fractional remainder of rate * dt
    uint32_t next_burst = 0;        // index into EmitterDesc::bursts
    uint32_t spawn_this_frame = 0;  // result of the last Advance
    uint32_t spawned_total = 0;
  };

  struct RingState {
    bool born = false;
    float birth_time = 0.0f;
    DirectX::XMFLOAT3 anchor_at_birth{0.0f, 0.0f, 0.0f};  // rings with follow = false stay where they were born
  };

  const SpellDesc* desc_;
  DirectX::XMFLOAT3 origin_;
  DirectX::XMFLOAT3 target_;
  uint32_t seed_;
  float time_ = 0.0f;
  float last_dt_ = 0.0f;                // for AnchorVelocity
  std::vector<EmitterState> emitters_;  // one per desc_->emitters entry (ring entries unused)
  std::vector<RingState> rings_;        // one per desc_->emitters entry (particle entries unused)
};

}  // namespace patronus::vfx
