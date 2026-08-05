# ADR 0001: Tech stack

Status: Accepted
Date: 2026-08-05

## Context

Patronus is a solo, from-scratch renderer built as a portfolio piece and
learning vehicle for a Graphics/VFX programming role. The stack needs to (a)
be current with what studios are actually shipping against today, (b)
support the kind of GPU-driven, compute-heavy work that role implies
(particles, render graphs), and (c) not burn time on tooling problems
instead of graphics problems.

## Decision

**Direct3D 12, Windows/MSVC only, C++20, HLSL Shader Model 6.6, DirectX
Agility SDK.**

### Why DX12

It's the current-generation API on the platform this role targets, with
explicit control over command lists, resource barriers, and descriptor
management — the exact areas a Graphics/VFX role expects hands-on depth in.
Targeting DX11 or an abstraction layer would hide the parts of the API
surface an interviewer will actually ask about. Single-platform (no Vulkan/
Metal backend) is a deliberate scope cut: depth on one modern API beats
shallow coverage of three.

### Why C++20

Modern-enough to use concepts, ranges (where they help, not as parser
puzzles), and `<format>`-adjacent conveniences, while still being the
version every current graphics/engine codebase you'd be reviewed against is
either on or moving to. Not C++23 — MSVC/toolchain support for it is still
less uniform, and there's no engine-relevant feature this project needs
that C++20 lacks.

### Why the Agility SDK

Windows only ships a D3D12 core DLL tied to the installed OS build.
Redistributing a pinned Agility SDK version means the project runs against
a known, current D3D12 runtime — including current debug-layer and
GPU-Based Validation behavior — regardless of what OS build the machine
(or an interviewer's machine) happens to be on. See
`src/rhi/AgilitySDKExports.cpp` and `cmake/FetchAgilitySDK.cmake` for the
mechanics. DXC (the HLSL SM 6.6 compiler) is fetched the same way, but note
it is **not** bundled inside the Agility SDK package — it's a separate
NuGet package (`Microsoft.Direct3D.DXC`), fetched by
`cmake/FetchDXC.cmake`.

### Why these three dependencies

- **Tracy** — frame-level CPU/GPU profiler. Instrumenting the renderer from
  day one means profiling data exists before there's a performance problem
  to chase, and Tracy's D3D12 GPU-zone support is a direct fit.
- **Dear ImGui (docking branch)** — debug UI for runtime-tunable renderer
  and particle-system state. The docking branch specifically, since
  multi-window debug tooling (separate profiler/inspector panels) is worth
  having from the start rather than retrofitting later. No CMakeLists.txt
  ships upstream, so `cmake/Dependencies.cmake` builds it as a plain static
  library, Win32 + DX12 backends only.
- **D3D12MemoryAllocator** — AMD's GPU memory suballocator. Hand-rolling a
  correct, defragmentation-aware D3D12 heap allocator is its own multi-week
  project that doesn't teach anything this role specifically needs;
  depending on the library everyone in the industry already depends on is
  the more honest use of the time.

All three, plus the Agility SDK, are pinned to specific tags/commits (see
`cmake/Dependencies.cmake`, `cmake/FetchAgilitySDK.cmake`,
`cmake/FetchDXC.cmake`) rather than tracking a moving branch — reproducible
builds matter more here than always being on the bleeding edge.

## Consequences

- No cross-platform portability; a Vulkan or Metal backend would need a
  real RHI abstraction layer this project deliberately doesn't have.
- Every machine that builds this needs network access at configure time
  (NuGet + GitHub) — nothing is vendored. Acceptable for a solo project;
  would need revisiting for a team or an offline CI environment.
- Pinned dependency versions mean security/bugfix updates are manual, not
  automatic — a deliberate tradeoff for reproducibility over freshness.
