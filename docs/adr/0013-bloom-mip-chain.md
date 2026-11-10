# ADR 0013: Bloom as a progressive mip chain in compute

Status: Accepted (retroactive; written after the first implementation)
Date: 2026-11-10

## Context

ADR-0003 renders the scene into an `R11G11B10_FLOAT` target so that
emissive values far above 1.0 survive until bloom and the tonemap. The
roadmap asked three questions before M1 bloom: the resolution chain, the
position relative to the tonemap, and whether to threshold.

- **Position.** Bloom models light scattered in a lens or the eye, which is
  linear in energy: a pixel at 20 should scatter twenty times as much light
  as a pixel at 1. After the tonemap that ratio is gone, so bloom runs
  before it.
- **Wide radius for little cost.** A blur of radius R at full resolution
  costs O(R²) taps, or O(R) when separable. A 3×3 kernel applied at mip k
  covers 3·2ᵏ full-resolution pixels, and the whole chain is about 1.33× the
  pixels of its first level.
- **In-place filtering of one UAV** was considered and rejected. Thread
  groups in one dispatch are unordered, so a neighbourhood read races with
  the writes of neighbouring groups. It also destroys the sharp image the
  composite needs, and a UAV cannot be sampled, which loses the bilinear
  taps the filter relies on.
- **Separable Gaussian ping-pong** is correct, but its cost grows with the
  radius and the wide glow needs two passes per level.

Reference: Jorge Jimenez, *Next Generation Post Processing in Call of Duty:
Advanced Warfare*, SIGGRAPH 2014.

## Decision

Compute-shader bloom runs between the scene pass and the tonemap pass, on a
dedicated texture.

- **`bloom_resource_`**: `R11G11B10_FLOAT`, half resolution, 5 mips,
  `ALLOW_UNORDERED_ACCESS`. Each mip has one SRV (`MostDetailedMip = i`,
  `MipLevels = 1`) and one UAV (`MipSlice = i`). Mip 0 starts at half
  resolution, so the scene is never copied at full resolution.
- **Downsample `down0`..`down4`.** Pass i reads its source through an SRV
  (`scene_texture_` for `down0`, mip i−1 after that) and writes mip i
  through a UAV. It takes 13 bilinear taps covering 6×6 source texels,
  arranged as five overlapping 2×2 blocks weighted 0.5 (centre) and 0.125
  (each corner).
- **`down0` only**: a Karis average (each block weighted by 1 / (1 + luma))
  against fireflies from single very bright particles, then a soft-knee
  threshold (defaults: threshold 0.54, knee 0.3).
- **Upsample `up3`..`up0`.** Pass i reads mip i+1 through an SRV with a 3×3
  tent (1 2 1 / 2 4 2 / 1 2 1, divided by 16, radius scaled by
  `upsample_scale`) and adds the result to mip i with a UAV load and store
  of the thread's own texel.
- **Composite** in the tonemap pixel shader, before exposure:
  `hdr += intensity × bloom / 5`. Bloom is sampled bilinearly from mip 0
  with full-resolution UV.
- **States are tracked per subresource.** A mip is `UNORDERED_ACCESS` while
  it is written and `NON_PIXEL_SHADER_RESOURCE` while it is read. The whole
  resource goes to `PIXEL_SHADER_RESOURCE` for the tonemap and back to
  `NON_PIXEL_SHADER_RESOURCE` at the end of the frame.

The bloom is thresholded and added, not CoD's
`lerp(scene, bloom, ~0.04)`. With a threshold, only light above it glows,
and that light comes on top of the scene. The lerp variant conserves energy
and belongs with no threshold.

## Consequences

- An SRV per mip means a view never spans a subresource that is in UAV
  state. This costs 10 descriptors, and binding stays trivial.
- All filter weights sum to 1, so each level has roughly the average
  brightness of the scene. Mip 0 after the upsample is the sum of five
  levels, and the composite divides by 5 to keep bloom comparable to the
  scene.
- Memory: about 1.33 × half resolution × 4 bytes, roughly 2.8 MB at 1080p.

### The typed UAV load of `R11G11B10_FLOAT` is not guaranteed

The upsample reads its own texel from a `RWTexture2D`, which is a typed UAV
load, in `R11G11B10_FLOAT`. D3D12 splits typed UAV load formats into three
groups:

1. `R32_FLOAT`, `R32_UINT` and `R32_SINT`: supported on all hardware.
2. Fifteen formats supported as a set and reported by
   `TypedUAVLoadAdditionalFormats`. The feature-level table counts them from
   12_0 upwards.
3. Formats that are optional **individually, at every feature level**.

`R11G11B10_FLOAT` is in group 3.

The device is created at feature level 12_2 (ADR-0009), and that does not
cover this case, because no feature level requires typed UAV loads for
`R11G11B10_FLOAT`. Forcing a feature level would not be a good fix even if
it did: it narrows the supported hardware generations to secure a single
format. Here it is not a fix at all. Creating the PSO also does not catch
it, because the shader flag the runtime checks only covers the group-2 set.
On hardware without per-format support the result is undefined. The
development GPU supports the format, so the bug is latent.

Possible solutions:

0. **Query at startup and fail fast (ADR-0008).** Call
   `CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT)` with
   `Format = DXGI_FORMAT_R11G11B10_FLOAT` and test
   `Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD`. This turns undefined
   behaviour into a clear error. It provides no fallback.
1. **A separate upsample chain.** A second texture with the same mips. The
   upsample pass for mip i reads the downsample of mip i through an SRV,
   reads the upsample result of mip i+1 through an SRV, and writes mip i of
   the upsample texture through a UAV, as a store only. No UAV load remains,
   and everything is read through SRVs with bilinear filtering. Cost: about
   1.33 × half resolution × 4 bytes of extra memory (a few MB). The root
   signature gets a table of 2 SRVs, which must be contiguous in the heap.
   The structure stays the same as now.
2. **Upsample as a pixel shader with additive blending.** Draw a fullscreen
   triangle into mip i as a render target with blend `ONE + ONE`. The ROP
   (the blend hardware) does the read-modify-write, and blending into an
   `R11G11B10_FLOAT` render target is widely supported. This requires
   `ALLOW_RENDER_TARGET` on the resource, an RTV per mip, a graphics PSO
   with that blend state, and RT ↔ SRV transitions per mip. It needs no
   extra memory, but it mixes compute and graphics in one chain.
3. **An `R16G16B16A16_FLOAT` bloom texture.** That format is in group 2, so
   the current 12_2 requirement guarantees it. It doubles the memory and
   bandwidth of the chain (8 bytes per texel instead of 4) and changes
   nothing structurally.

### Known debt, deferred until after measurement

The order is: implement, measure, then improve the code and the
architecture. These items are known and intentionally left for now:

- The `/ 5` in `tonemap.hlsl` is coupled to `mip_levels` in
  `CreateBloomResource`. Changing the mip count silently changes the bloom
  brightness.
- The tonemap receives the whole `BloomConstants` block, and `dst_size` is
  overwritten in the shared `bloom_constants_` just before the tonemap
  pass. This works only because the bloom passes rewrite it every frame.
- The Karis (first-pass) bit is cleared after `down0` and set again at the
  end of the bloom block for the next frame, instead of being set per
  dispatch.

### To measure

Bloom gets its own timestamp zone (roadmap M1, ADR-0011). Prediction:
`down0` takes about 75% of the downsample time, because it reads three
times as many pixels as all later downsamples together
(1/4 + 1/16 + … = 1/3). If it comes out much lower, the chain is bound by
fixed per-dispatch or per-barrier overhead rather than bandwidth.
