# 2026-09-12: GPU Particle Foundations — Compute Sim, Camera, Vertex Pulling

*Notes on the implementation, drafted with Claude's help from the code and
commit history — same disclosure as the first devlog entry.*

Five commits over two days turned the triangle demo into a GPU-driven
particle system: a compute-shader simulation, a perspective camera, and a
vertex-pulled billboard renderer to draw the result.

## Particle storage: one `RWStructuredBuffer`, two views

Particle state is a single GPU buffer of:

```hlsl
struct Particle
{
  float3 pos;
  float3 vel;
  float lifetime;
  float _pad;
};
```

(`computeshader.hlsl`, `particleshader.hlsl`). `_pad` exists because
`float3` is 12 bytes but HLSL packs structured-buffer elements on 16-byte
boundaries — without it, `pos`/`vel`/`lifetime` would straddle a boundary
inconsistently between how the CPU writes it and how HLSL indexes it.

The same buffer is bound twice, as two different view types, to two
different shader stages:

- **UAV** (`register(u0)`, compute shader) — read-write access, so the
  simulation can update every particle's position and velocity in place.
- **SRV** (`register(t0)`, vertex shader) — read-only access, so the
  renderer can pull particle data back out without ever writing to it.

This is the standard "simulate on the GPU, render straight from the
result" pattern: the particle data never round-trips through the CPU
after the initial upload, so there's no per-frame readback and no
CPU-side particle array to keep in sync.

## Vertex pulling: no vertex or index buffer at all

`particleshader.hlsl`'s `VSMain` takes `SV_VertexID`/`SV_InstanceID` and
does everything from there:

- `SV_InstanceID` indexes straight into the particle `StructuredBuffer` —
  `Particle p = gParticles[instanceID];` — no per-instance vertex buffer
  is ever bound.
- `SV_VertexID` (0–3) indexes a `static const` quad-corner table baked
  into the shader (`QuadCorners[4]`) — no per-vertex buffer either.
- The draw call is `DrawInstanced(4, kParticleCount, 0, 0)`-shaped: 4
  vertices per instance, one instance per particle, and every vertex's
  actual attributes are computed inline in the shader from the two IDs
  above rather than fetched from bound geometry.

This is "vertex pulling" — the vertex shader pulls its own input data out
of a `StructuredBuffer` by index, instead of the Input Assembler pushing
data in through a bound vertex/index buffer. For a particle system it
removes an entire category of per-frame CPU work (no rebuilding a vertex
buffer to match the simulation's current positions) at the cost of one
extra SRV bind and an index computation per vertex — a straightforward
win once the data already lives in a structured buffer for the compute
pass anyway.

## Billboarding

Each particle's quad is built directly in world space from the camera's
own basis vectors, uploaded per frame in `CameraCB`:

```hlsl
float3 worldPos = p.pos
                + gCamRight * corner.x * gBillboardSize
                + gCamUp    * corner.y * gBillboardSize;
```

Using the camera's actual right/up vectors (rather than, say, the world
X/Y axes) is what makes the quad always face the camera regardless of
where it's looking — a screen-aligned billboard, not a fixed-orientation
quad.

## Camera: a small scene-object hierarchy, not a one-off

`Camera3D` is a `GameObject3D`, which is a `GameObject` — a small,
reusable hierarchy (position/rotation base class → direction-vector
bookkeeping → camera-specific view/projection matrices) rather than a
particle-demo-only camera struct. `GameObject3D` maintains forward/
left/right/backward/up as cached `XMVECTOR`s, recomputed via
`UpdateDirectionVectors()` whenever rotation changes, so `WASD`-style
movement (`OnKeyDown` in `D3D12HelloTriangle.cpp`) just adds a scaled
direction vector to the position rather than re-deriving direction from
Euler angles every frame.

## Frame timing → the simulation, via a root constant

`HPTimer` wraps `QueryPerformanceCounter` for a monotonic, sub-millisecond
frame clock, and also folds in a 60 FPS soft cap (`Update()` busy-waits
in 1ms `Sleep` increments, bracketed by `timeBeginPeriod(1)`/
`timeEndPeriod(1)` to get the scheduler's finer timer granularity —
without that, `Sleep(1)` can actually sleep closer to 15ms on default
Windows timer resolution). Its `GetDeltaTime()` feeds a
`SimulationConstants` root constant (`gDeltaTime`, `gParticleCount`) read
directly by the compute shader — no constant *buffer* round-trip needed
for two scalars this small, a root constant is cheaper for data that
changes every dispatch.

## Semi-implicit (symplectic) Euler

The first working integrator (now superseded by curl noise — see the next
devlog entry, both versions are still in `computeshader.hlsl`, one
commented out) is:

```hlsl
p.vel += gravity * gDeltaTime;  // v(t) = v0 + a * t
p.pos += p.vel * gDeltaTime;    // x(t) = x0 + v * t
```

The "semi-implicit" part is the order: velocity is updated *first*, using
the *old* velocity to update position would be explicit Euler. Updating
position with the *already-advanced* velocity is what makes this
symplectic — it doesn't exactly conserve energy, but it doesn't
systematically gain or lose energy either, which is why gravity-driven
motion under it looks physically stable indefinitely instead of visibly
drifting or exploding over time the way plain explicit Euler eventually
does at larger timesteps.
