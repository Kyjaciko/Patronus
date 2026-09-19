# 2026-09-15: 3D Curl Noise Advection & Mouse-Look Camera

*Notes on the implementation, drafted with Claude's help from the code and
commit history — same disclosure as the first devlog entry.*

## Why curl noise

Gravity-only motion (previous entry) falls apart quickly as a visual — it
just accelerates downward forever. Curl noise is a standard way to get
turbulent, fluid-like motion for a particle system without running an
actual fluid simulation: instead of generating a random *velocity* field
directly, generate a random scalar/vector **potential** field and take
its curl. The curl of any field is mathematically guaranteed to be
divergence-free (∇·(∇×Ψ) ≡ 0), so the resulting velocity field never has
sources or sinks — particles swirl and interweave without ever visibly
clumping into or draining out of a point. That divergence-free property
is the entire reason to bother with curl noise instead of, say, raw
Perlin/simplex noise sampled directly as a velocity: raw noise has no
such guarantee and tends to visibly pool.

## Baking the field offline (`tools/curl noise generator/curlnoise.py`)

The field is precomputed once, offline, rather than evaluated live on the
GPU per particle per frame:

1. Generate three independent 3D OpenSimplex noise fields
   (`Ψx, Ψy, Ψz`, one per axis, different seeds) over a 64³ grid spanning
   a fixed world-space region (`FIELD_MIN`/`FIELD_MAX = ±10` on each axis
   — this range has to match what the shader assumes, see below).
2. Take the curl of the resulting vector potential field via central
   finite differences (`EPSILON` = mean grid spacing) — this is the step
   that turns three arbitrary noise fields into one divergence-free
   velocity field.
3. Export the 64×64×64×4 (RGBA16F) result as a raw `.bin` (consumed by
   the renderer), a `.json` (metadata — grid size, world bounds, so the
   shader-side mapping stays traceable to the file that produced it), and
   a `.png` preview slice for sanity-checking by eye before it ever
   touches the GPU.

Baking offline instead of evaluating simplex noise + finite differences
per-particle per-frame on the GPU trades a fixed one-time cost (and a
fixed memory footprint — one 64³ RGBA16F volume, sampled with hardware
trilinear filtering) for a large recurring one. It also means the field
is authored data that can be inspected, versioned, and swapped, rather
than something implicitly defined by shader code.

## Sampling it in the compute shader

```hlsl
float3 uvw = (p.pos - worldMin) / worldSize;
float3 curlForce = gCurlNoiseTexture.SampleLevel(gSampler, uvw, 0).xyz;
p.vel += curlForce * curlStrength * gDeltaTime;
```

`worldMin`/`worldSize` in `computeshader.hlsl` are a second, hardcoded
copy of the same bounds the Python tool baked the field against (with a
comment pointing back at the `.json` file) — not derived from it
automatically. That's a real coupling to watch: if the tool's
`FIELD_MIN`/`FIELD_MAX` ever change, the shader constants have to be
updated by hand or particles will sample outside the intended `[0,1]`
UVW range.

## The "orb" effect: a spherical attracting band

Pure curl noise alone just scatters particles outward indefinitely (the
`3D_curl_noise_no_center_attracting_force.gif` in the README's gallery).
The orb-shell look in the hero gif adds one more force on top: a radial
pull that only activates once a particle strays outside a band around a
target radius from the origin —

```hlsl
float error = (distToCenter > outerRadius)
  ? (distToCenter - outerRadius)
  : (distToCenter - innerRadius);
p.vel += directionToCenter * error * pullStrength * gDeltaTime;
```

— i.e. it's a spring-like restoring force clamped to a `[innerRadius,
outerRadius]` band rather than pulling toward an exact radius, which
lets curl noise still dominate the fine-grained motion *within* the band
while the whole system stays loosely shell-shaped instead of drifting
apart. A velocity damping term (`p.vel *= 1 - saturate(5 * gDeltaTime)`)
is applied after both forces so accumulated velocity doesn't grow
unbounded over a long-running session.

## Mouse-look

`Mouse`/`MouseEvent` (new this session) is a small win32-input →
event-queue adapter: `Win32Application`'s message loop pushes
`OnMouseMove`/`OnMouseRawMove`/button events onto `Mouse`'s internal
`std::queue<MouseEvent>`, and `D3D12HelloTriangle::OnUpdate` drains that
queue once per frame rather than reacting inside the window procedure
directly. Camera rotation only applies raw-move deltas while the right
button is held (`m_mouse->IsRightPressed()`) — checked against `RAW_MOVE`
events specifically, not plain `MOVE`, since raw input deltas aren't
clamped to the screen edges the way cursor-position-based `MOVE` events
are, which matters once the cursor would otherwise hit the edge of the
window during a fast look. Pitch is clamped to just under ±90°
(`XM_PIDIV2 - 0.01f`) after each update to stop the camera from flipping
upside down.

## Addendum (2026-09-17): which bake is used, and why it was replaced

Two baked volumes were committed: `curl_noise_64x64x64_rgba16f` with
`NOISE_SCALE = 0.18` and `..._type2` with `0.85`. The shader loads
`type2`. At 0.18 the potential varies about once across the whole 20-unit
box, so the curl is one or two large swirls and the orb reads as a slow
rotation; at 0.85 there are several curls per axis and the shell gets the
turbulent surface in the hero gif. The first bake was kept for comparison
and should be deleted once the loader moves to the new asset.

The generator itself has been superseded by `tools/bake_curl_noise.py`
for reasons found in the 2026-09-17 review:

- The field was not periodic (plain noise over a linear range, one-sided
  differences at the edges) but the sampler used `WRAP`, and the orb band
  (radius 7.5 to 12.5) extends past the +/-10 box, so many particles
  sampled across a discontinuity every frame.
- The alpha channel was written as 0 and fetched anyway.
- The world bounds lived twice: in the tool's JSON and hardcoded in the
  shader.

The new bake generates noise on a periodic lattice with a wrapping curl
stencil (verified divergence-free to float precision), puts a scalar
fractal noise in alpha, and emits a `.json` and a constexpr `.h` so the
dimensions and tile size are read, not retyped. Output:
`assets/noise/curl_noise_64_rgba16f.*`, deployed next to the exe.
