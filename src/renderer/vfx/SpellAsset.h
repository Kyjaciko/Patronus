// Spell authoring data: the parsed form of assets/emitters/<spell>.json.
//
// Skeleton written by Claude under a one-time, explicitly authorised
// exception to the CLAUDE.md hard rule (2026-09-18): structs, classes and
// empty functions with comments only. Bodies are written by the user.
//
// Pure data, no D3D12. SpellDesc is what an artist edits (in the JSON or
// through VfxSystem::DrawDebugUi) and what SpellInstance plays. Field names
// follow the JSON keys, which are also the EmitterSpec fields of
// tools/vfxtools/previs.py, so the previs stays the CPU twin of the runtime
// (ADR-0010).

#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

#include "renderer/vfx/VfxTypes.h"

namespace patronus::vfx {

// The "ring" block of a kind = "ring" emitter. Static values; RingParams is
// the per-frame row derived from it.
struct RingDesc {
  DirectX::XMFLOAT3 axis{1.0f, 0.0f, 0.0f};
  uint32_t points = 120;
  float arc_degrees = 360.0f;
  uint32_t count = 1;
  float phase_degrees = 0.0f;
  float radius = 1.0f;
  float expand = 0.0f;
  float omega = 0.0f;
  float wave_amp = 0.0f;
  float wave_freq = 3.0f;
  float wave_speed = 0.5f;
  float axial_offset = 0.0f;
  float axial_velocity = 0.0f;
};

// One emitter as authored. The static half of EmitterParams plus the fields
// only the CPU needs (timeline window, rate, bursts, pool size, curve names).
struct EmitterDesc {
  enum class Kind : uint32_t { kParticles = 0, kRing = 1 };

  std::string name;
  Kind kind = Kind::kParticles;
  Attach attach = Attach::kProjectile;
  bool follow = false;        // EmitterParams::follow_anchor
  float active_start = 0.0f;  // [start, end) window for rate emission, spell seconds
  float active_end = 0.0f;
  float rate = 0.0f;                               // particles per second inside the window
  std::vector<std::pair<float, uint32_t>> bursts;  // (time, count); a ring emitter bursts once with count 1
  uint32_t pool = 256;                             // GPU slots; the previs report's max_alive is the number to put here
  float life_min = 1.0f;
  float life_max = 1.0f;

  // Curve names resolve to atlas rows at load time through the atlas
  // metadata JSON (tools/bake_curves.py writes rows: {name: {row, ...}}).
  std::string colour_curve;
  std::string size_curve;

  // Everything the GPU reads, with the per-frame fields (anchor, spawn_count,
  // time, pool range, seed) left zero here and filled by SpellInstance.
  EmitterParams params{};

  // Only read when kind == kRing.
  RingDesc ring;
};

// The whole JSON file.
struct SpellDesc {
  std::string source_path;  // where it was loaded from; Save writes back here by default

  // Asset paths relative to the JSON file, without extension: the bakers
  // write <stem>.bin + <stem>.json next to each other.
  std::string curve_atlas;
  std::string curl_noise;
  std::string noise_pack;

  // Timeline knots (see previs Timeline): the projectile anchor moves from
  // origin to target between charge_end and impact; the spell ends at end.
  DirectX::XMFLOAT3 origin{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 target{0.0f, 0.0f, 0.0f};
  float charge_end = 0.0f;
  float impact = 0.0f;
  float end = 0.0f;

  // Render hints the app may apply (exposure, bloom); the runtime does not
  // depend on them.
  float exposure = 1.0f;
  float bloom = 0.0f;

  std::vector<EmitterDesc> emitters;  // JSON order is draw order
};

// Loads a spell JSON into a SpellDesc. Returns false and leaves out_desc
// untouched when the file is missing or malformed; the message names the
// emitter and key at fault so an artist can fix the JSON without a debugger.
//
// Parsing needs a JSON library (nlohmann/json via FetchContent is the
// obvious candidate) or a bake step that writes a flat binary; that
// decision is open (ADR-0010 consequences) and taken before this body is
// written.
bool LoadSpell(const std::string& path, SpellDesc& out_desc, std::string& out_error);

// Writes a SpellDesc back as JSON with the same key names, so the previs and
// the runtime keep reading one file. Used by the debug UI "Save" button.
bool SaveSpell(const SpellDesc& desc, const std::string& path, std::string& out_error);

// Resolves colour_curve / size_curve names to rows through the atlas metadata
// JSON (<curve_atlas>.json) and writes them into params.size_curve_row and
// params.colour_curve_row (kNoCurve when the name is empty). Returns false
// when a named curve does not exist in the atlas.
bool ResolveCurveRows(const std::string& atlas_metadata_path, SpellDesc& desc, std::string& out_error);

}  // namespace patronus::vfx
