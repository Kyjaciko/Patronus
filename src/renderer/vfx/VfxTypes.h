// GPU-shared data contract of the VFX system.
//
// Skeleton written by Claude under a one-time, explicitly authorised
// exception to the CLAUDE.md hard rule (2026-09-18): structs, classes and
// empty functions with comments only. No D3D12 calls, no HLSL, no logic.
// Everything from here on is written and owned by the user.
//
// Every struct in this file has a twin in shaders/vfx/VfxTypes.hlsli with
// the same fields in the same order. This file is the source of truth; the
// HLSL twin is mirrored by hand and guarded by the static_asserts below
// (sizes) and by review (order). Rules that keep the two in sync:
//
//   - every struct is a multiple of 16 bytes, padding is explicit and named;
//   - no bool (4-byte uint32_t instead), no double, no nested arrays;
//   - float3 members never straddle a 16-byte boundary, so the HLSL packing
//     rules for constant buffers and structured buffers give the same layout.
//
// Decisions: ADR-0005 (lists, counters, indirect arguments), ADR-0006
// (premultiplied blend), ADR-0010 (data-driven emitters, one facade, the
// class layout of src/renderer/vfx/).

#pragma once

#include <DirectXMath.h>

#include <cstdint>

namespace patronus::vfx {

// Spawn shapes. Uniform per emitter, so the branch in the spawn kernel costs
// nothing. Numeric values are shared with the HLSL twin; append, never
// reorder.
enum class SpawnShape : uint32_t {
  kSphere = 0,  // inside (or on the surface of) a sphere around the anchor
  kRing = 1,    // around spawn_axis, with coherent arms, phase rate, pitch (helix), jitter, cone
};

// Pixel-shader shape SDF. Uniform per emitter draw (ADR-0005: one
// ExecuteIndirect command per emitter), so no divergence inside a draw.
enum class ParticleShape : uint32_t {
  kDisc = 0,    // soft radial falloff, eroded by noise
  kMote = 1,    // tiny bright point with a wide faint halo
  kRing = 2,    // thin annulus
  kStreak = 3,  // velocity-aligned line (uses RenderParticle::vel and stretch)
};

// Which timeline anchor an emitter follows. CPU-only (SpellInstance resolves
// it to a position before the row is written), kept next to the enums the
// GPU sees so the JSON vocabulary lives in one place.
enum class Attach : uint32_t {
  kOrigin = 0,      // where the cast starts
  kProjectile = 1,  // moves origin -> target between charge_end and impact
  kImpact = 2,      // the target, once impact has happened
};

// Simulation state of one particle. AoS: one thread reads and writes the
// whole record in the simulate kernel. Positions are in emitter space (see
// EmitterParams::follow_anchor). 48 bytes: three 16-byte rows.
struct Particle {
  DirectX::XMFLOAT3 pos;  // emitter space
  float age;              // seconds since spawn
  DirectX::XMFLOAT3 vel;  // emitter space, units per second
  float life;             // seconds; dead when age >= life
  uint32_t seed;          // per-particle hash seed (twinkle phase, noise offset, curve jitter)
  float rot;              // billboard rotation, radians
  float ang_vel;          // radians per second
  uint32_t pad0;
};
static_assert(sizeof(Particle) == 48, "Particle must match shaders/vfx/VfxTypes.hlsli");

// What the vertex shader needs and nothing else. Written by the simulate
// kernel for every particle that survives the frame, read four times per
// particle (one per quad corner), so it is kept separate from Particle.
// 48 bytes; packing vel into three halves would give 32 and is a
// measurement for M5, not a starting point.
struct RenderParticle {
  DirectX::XMFLOAT3 pos;  // world space (anchor already added when follow_anchor is set)
  uint32_t size_rot;      // two halves: size (world units), rotation (radians)
  DirectX::XMFLOAT3 vel;  // world space, for velocity stretch in the VS
  float age_norm;         // age / life in [0, 1); the curve atlas u coordinate
  uint32_t colour_rg;     // RGBA16F colour, already multiplied by the curve; HDR values allowed
  uint32_t colour_ba;
  uint32_t flags;  // bits 0..7 shape (ParticleShape), 8..15 additive share as unorm8, 16..31 emitter index
  uint32_t pad0;
};
static_assert(sizeof(RenderParticle) == 48, "RenderParticle must match shaders/vfx/VfxTypes.hlsli");

// One row per emitter per frame, written by SpellInstance::WriteParams into
// the ring slot of the current frame in flight, read by all three kernels
// and both shaders. Field names are the JSON keys of assets/emitters/*.json
// (and the EmitterSpec fields of tools/vfxtools/previs.py) so a value can be
// found from either side. A zero strength disables a force; there is no
// per-emitter code path (ADR-0010). 288 bytes: eighteen 16-byte rows.
struct EmitterParams {
  // --- per frame (SpellInstance) ---
  DirectX::XMFLOAT3 anchor_pos;  // world space anchor this frame
  uint32_t spawn_count;          // particles to spawn this frame; the only count the CPU produces (ADR-0005)
  DirectX::XMFLOAT3 anchor_vel;  // world space, for vel_inherit
  float time;                    // spell time, seconds (ring/helix phase, curl scroll, twinkle)
  uint32_t follow_anchor;        // 1: particles live in emitter space and the VS adds anchor_pos every frame
  uint32_t pool_first;           // first slot of this emitter's range in the shared pool buffers
  uint32_t pool_count;           // slots in that range (the JSON "pool" field)
  uint32_t frame_seed;           // hashed with the thread id at spawn

  // --- spawn ---
  DirectX::XMFLOAT3 spawn_axis;  // ring/helix axis, unit length, emitter space
  SpawnShape spawn_shape;
  float spawn_radius;
  uint32_t spawn_surface;  // 1: on the surface, 0: in the volume
  uint32_t spawn_arms;     // ring: number of coherent spiral arms
  float spawn_phase_rate;  // ring: radians per second the arm angle advances
  float spawn_jitter;      // ring: random angle spread, radians
  float spawn_axial_min;   // ring: offset range along the axis
  float spawn_axial_max;
  float spawn_cone;   // ring: radius grows by this per unit of axial offset
  float spawn_pitch;  // ring: angle advances by this per unit of axial offset (helix)
  float life_min;
  float life_max;
  float vel_inherit;  // fraction of anchor_vel added at spawn

  // --- initial velocity ---
  float vel_radial_min;  // along the spawn direction, away from the anchor
  float vel_radial_max;
  float vel_inward_min;  // along the spawn direction, toward the anchor
  float vel_inward_max;
  DirectX::XMFLOAT3 vel_direction;  // constant added velocity, emitter space
  float vel_spread;                 // random isotropic speed
  float vel_tangent_min;            // ring: around the axis, right-hand rule
  float vel_tangent_max;
  float pad0;
  float pad1;

  // --- forces (simulate kernel; formulas in the previs docstring) ---
  float gravity;                  // units per second squared along -y
  float drag;                     // 1/s: vel *= 1 - saturate(drag * dt)
  float curl_strength;            // units per second per unit field value
  float curl_scale;               // world units per curl volume tile
  DirectX::XMFLOAT3 curl_scroll;  // tiles per second of uvw drift (animates the field)
  float attract_strength;
  float attract_radius;           // band centre; 0 pulls to the anchor
  float attract_band;             // half width of the band with no force
  float vortex_spin;              // target tangential speed, units per second
  float vortex_spin_rate;         // 1/s relaxation toward vortex_spin
  DirectX::XMFLOAT3 vortex_axis;  // through the anchor, unit length
  float vortex_pull;              // band attractor toward the axis
  float vortex_radius;
  float vortex_band;
  float vortex_cone;   // band radius grows by this per unit of axial position
  float vortex_axial;  // acceleration along the axis

  // --- look (simulate writes the render record; VS/PS read it) ---
  float size;                 // base radius, world units, multiplied by the size curve
  uint32_t size_curve_row;    // row in the spell's curve atlas; kNoCurve = constant 1
  uint32_t colour_curve_row;  // row in the spell's curve atlas; kNoCurve = white
  float additive;             // 1 additive, 0 alpha blend, in between mixes (ADR-0006)
  ParticleShape shape;
  float erosion;      // 0..1 noise dissolve strength over life
  float twinkle;      // Hz alpha flicker; 0 off
  float stretch;      // seconds of velocity a streak is stretched by
  float ang_vel_min;  // radians per second, per-particle random in range
  float ang_vel_max;
  float soft_fade_distance;  // world units over which the soft-particle fade ramps (ADR-0004)
  float pad2;
};
static_assert(sizeof(EmitterParams) == 288, "EmitterParams must match shaders/vfx/VfxTypes.hlsli");

// Sentinel for size_curve_row / colour_curve_row when the JSON has no curve.
inline constexpr uint32_t kNoCurve = 0xFFFFFFFFu;

// One row per live ring per frame. A ring is kinematic geometry, not
// particles (ADR-0010): the ring vertex shader derives theta from
// SV_VertexID and this row from SV_InstanceID and evaluates
//   pos = anchor + axis * (axial_offset + axial_velocity * age)
//       + dir(theta + omega * age) * (radius + expand * age) * (1 + wave_amp * noise)
// as a camera-facing strip of points * 2 vertices. No vertex buffer exists.
// 112 bytes: seven 16-byte rows.
struct RingParams {
  DirectX::XMFLOAT3 anchor_pos;  // world space, sampled at the frame the ring was born or every frame (see follow)
  float age;                     // seconds since birth
  DirectX::XMFLOAT3 axis;        // unit length
  float life;                    // seconds; the system stops drawing the ring when age >= life
  uint32_t points;               // vertices around the full circle; arc rings use arc / 360 of them
  float arc_degrees;             // 360 for a full ring
  uint32_t count;                // copies spaced evenly around the axis (the two arcs are count = 2)
  float phase_degrees;           // start angle of copy 0
  float radius;
  float expand;      // radius growth, units per second
  float omega;       // rotation, radians per second
  float wave_amp;    // radial displacement as a fraction of radius
  float wave_freq;   // cycles around the ring
  float wave_speed;  // noise scroll, cycles per second
  float axial_offset;
  float axial_velocity;
  float width;                // strip half width, world units (the JSON "size")
  uint32_t colour_curve_row;  // sampled by age / life
  ParticleShape shape;        // kMote or kStreak; the strip's cross-section falloff
  float stretch;              // as for particles
  float additive;
  uint32_t copy_index;  // which of the count copies this row draws; SpellInstance expands count into rows
  float pad0;
  float pad1;
};
static_assert(sizeof(RingParams) == 112, "RingParams must match shaders/vfx/VfxTypes.hlsli");

// Per-emitter counters, 16 bytes each, in one RWByteAddressBuffer that stays
// in UNORDERED_ACCESS for the whole frame (ADR-0005). Offsets are
// emitter_index * sizeof(EmitterCounters) + the member offset. This struct
// is never declared in HLSL (byte-address buffers are addressed by offset),
// it only documents the layout and gives the C++ side the offsets.
struct EmitterCounters {
  uint32_t dead_count;     // free slots; spawn pops with InterlockedAdd(-1)
  uint32_t alive_count_a;  // ping-pong: (frame & 1) == 0 -> a is current, b is next; swapped every frame
  uint32_t alive_count_b;
  uint32_t render_count;  // RenderParticle records written this frame
};
static_assert(sizeof(EmitterCounters) == 16, "EmitterCounters must match shaders/vfx/VfxTypes.hlsli");

// Per-emitter indirect arguments, written by the kickoff kernel and (for the
// draw) the simulate kernel, in their own buffer so the counters above never
// need to leave UAV state. Layout matches the command signatures created by
// VfxPipeline: DISPATCH for spawn and simulate; CONSTANT + DRAW_INDEXED for
// the particle draw. 48 bytes.
struct EmitterIndirectArgs {
  uint32_t spawn_groups_x;  // ceil(spawn_count / kSpawnGroupSize)
  uint32_t spawn_groups_y;  // 1
  uint32_t spawn_groups_z;  // 1
  uint32_t sim_groups_x;    // ceil((alive_prev + spawn_count) / kSimGroupSize): an upper bound, the kernel early-outs
  uint32_t sim_groups_y;    // 1
  uint32_t sim_groups_z;    // 1
  uint32_t emitter_index;   // the CONSTANT argument: root constant the particle VS reads
  uint32_t index_count_per_instance;  // render_count * 6 (shared static index buffer)
  uint32_t instance_count;            // 1
  uint32_t start_index_location;      // 0
  int32_t base_vertex_location;       // 0
  uint32_t start_instance_location;   // 0
};
static_assert(sizeof(EmitterIndirectArgs) == 48, "EmitterIndirectArgs must match shaders/vfx/VfxTypes.hlsli");

// Root CBV, one per frame in flight (ADR-0002 rule: written after the fence
// wait, never while the GPU may read it). Descriptor indices are how SM 6.6
// ResourceDescriptorHeap[] finds every buffer and texture, so the root
// signature carries no tables.
struct FrameConstants {
  DirectX::XMFLOAT4X4 view;
  DirectX::XMFLOAT4X4 proj;
  DirectX::XMFLOAT4X4 view_proj;
  DirectX::XMFLOAT4 depth_linearise;  // constants that turn a reverse-Z depth sample into view distance (ADR-0004)
  DirectX::XMFLOAT3 cam_right;
  float dt;
  DirectX::XMFLOAT3 cam_up;
  float time;
  DirectX::XMFLOAT3 cam_pos;
  float pad0;
  DirectX::XMFLOAT2 resolution;
  DirectX::XMFLOAT2 inv_resolution;

  // Descriptor heap indices, filled by VfxResources. Two scalars rather than
  // uint[2]: in a constant buffer HLSL pads every array element to 16 bytes,
  // which would silently break the C++ mirror.
  uint32_t particles_uav;
  uint32_t render_particles_uav;
  uint32_t render_particles_srv;
  uint32_t alive_list_uav_a;
  uint32_t alive_list_uav_b;
  uint32_t dead_list_uav;
  uint32_t counters_uav;
  uint32_t indirect_args_uav;
  uint32_t emitter_params_srv;
  uint32_t ring_params_srv;
  uint32_t curve_atlas_srv;
  uint32_t curl_volume_srv;
  uint32_t noise_pack_srv;
  uint32_t scene_depth_srv;
  uint32_t emitter_count;
  uint32_t ring_count;
};
static_assert(sizeof(FrameConstants) % 16 == 0, "FrameConstants is a constant buffer");

// Thread-group sizes shared with the [numthreads] attributes in the kernels
// and used by the kickoff kernel to size the indirect dispatches.
inline constexpr uint32_t kSpawnGroupSize = 64;
inline constexpr uint32_t kSimGroupSize = 64;

}  // namespace patronus::vfx
