# ADR 0009: Feature level 12_2 required; debug layer breaks on error

Status: Accepted (retroactive; decision made 2026-08-10, written 2026-09-17)
Date: 2026-08-10

## Context

The sample creates its device at `D3D_FEATURE_LEVEL_11_0`, the lowest
level D3D12 supports, so that it runs on anything. This project's stated
purpose (ADR-0001) is depth on current GPU-driven techniques: mesh
shaders, Shader Model 6.5/6.6, bindless descriptors, and later a
work-graph experiment. Those are optional features at 11_0 and each would
need its own capability check and fallback.

`D3D_FEATURE_LEVEL_12_2` (DirectX 12 Ultimate) is defined as a bundle:
DXR 1.1, mesh and amplification shaders, variable-rate shading tier 2,
sampler feedback, resource binding tier 3 and Shader Model 6.5. Hardware:
NVIDIA Turing and later, AMD RDNA 2 and later, Intel Arc. Anything older
fails device creation.

Separately, the debug layer is enabled in `_DEBUG` builds and the
`ID3D12InfoQueue` is set to break into the debugger on `ERROR` and
`CORRUPTION` messages. GPU-Based Validation is enabled only in the smoke
test, not in the demo.

## Decision

The demo requires `D3D_FEATURE_LEVEL_12_2` at device creation
(`D3D12HelloTriangle::LoadPipeline`), for both the hardware adapter and
the WARP path. There are no fallbacks for the features the level
guarantees; the roadmap (mesh-shader draw path, SM 6.6 bindless) relies on
that.

Debug builds enable the debug layer and break on error/corruption so a
validation error stops at the offending call instead of scrolling past in
the output window. GPU-Based Validation stays off in the demo by default
because it slows the particle dispatches by an order of magnitude; it is
turned on manually when a UAV-ordering bug is suspected (see ADR-0005 on
UAV barriers, which the ordinary debug layer does not check).

## Consequences

- Hardware support is narrowed to 2018-and-later discrete GPUs and
  recent Intel. Acceptable for a portfolio demo aimed at graphics
  programmers; not acceptable for a shipped product without a fallback.
- Every capability the roadmap uses can be assumed rather than queried.
  Work graphs are the exception (not part of any feature level) and must
  still be queried through `D3D12_FEATURE_D3D12_OPTIONS21` (ADR-0007).
- The WARP path requires a WARP that supports 12_2, which depends on the
  Windows build. CI does not run the demo, only the smoke test.
- The smoke test (`src/rhi/smoketest`) still probes `11_0` so it runs on
  GPU-less CI runners. It therefore proves the build and the Agility SDK
  deployment, not that a machine can run the demo. Open follow-up: have
  the smoke test also report the highest supported feature level and
  the mesh-shader and work-graph tiers, so the gap is visible in the CI
  log instead of implicit.
- Breaking on error means a validation error in a Debug build is
  effectively fatal under a debugger. That is intended: the project's
  standard is a clean debug layer, not a tolerated warning list.
