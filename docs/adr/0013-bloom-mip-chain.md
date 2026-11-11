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

### Figure 1: resource, views and heap slots

```
bloom_resource_   R11G11B10_FLOAT   MipLevels = 5   ALLOW_UNORDERED_ACCESS   initial state NPSR
 ├─ mip0   W/2  × H/2     ◀── SRV[0]   UAV[0]
 ├─ mip1   W/4  × H/4     ◀── SRV[1]   UAV[1]
 ├─ mip2   W/8  × H/8     ◀── SRV[2]   UAV[2]
 ├─ mip3   W/16 × H/16    ◀── SRV[3]   UAV[3]
 └─ mip4   W/32 × H/32    ◀── SRV[4]   UAV[4]          mip k = (W >> (k+1)) × (H >> (k+1))

scene_texture_    W × H                ◀── SRV (SceneTexture)

m_particleSrvUavHeap
 [ PoolUAV | CurlNoiseSRV | PoolSRV | DearImGui | SceneTexture | SoftParticle | BloomSrv0..4 | BloomUav0..4 | Count ]
```

### Figure 2: passes and resource states

```
          READ (SRV)        WRITE (UAV)       BARRIERS BEFORE
scene     —                 scene_texture_    scene : PSR|NPSR → RT
                                              (after) scene : RT → PSR|NPSR
─────────────────────────────────────────────────────────────────────────────────────
down0     scene_texture_    mip0              mip0 : NPSR → UAV
down1     mip0              mip1              mip0 : UAV → NPSR      mip1 : NPSR → UAV
down2     mip1              mip2              mip1 : UAV → NPSR      mip2 : NPSR → UAV
down3     mip2              mip3              mip2 : UAV → NPSR      mip3 : NPSR → UAV
down4     mip3              mip4              mip3 : UAV → NPSR      mip4 : NPSR → UAV
─────────────────────────────────────────────────────────────────────────────────────
up3       mip4              mip3  (+=)        mip4 : UAV → NPSR      mip3 : NPSR → UAV
up2       mip3              mip2  (+=)        mip3 : UAV → NPSR      mip2 : NPSR → UAV
up1       mip2              mip1  (+=)        mip2 : UAV → NPSR      mip1 : NPSR → UAV
up0       mip1              mip0  (+=)        mip1 : UAV → NPSR      mip0 : NPSR → UAV
─────────────────────────────────────────────────────────────────────────────────────
tonemap   scene + mip0      back buffer       mip0 : UAV → NPSR      all mips : NPSR → PSR
end       —                 —                 all mips : PSR → NPSR   (after ImGui)

bloom off: only   all mips : NPSR → PSR   and   all mips : PSR → NPSR
RT = RENDER_TARGET   NPSR = NON_PIXEL_SHADER_RESOURCE (compute reads)   PSR = PIXEL_SHADER_RESOURCE (tonemap / ImGui read)
```

### Figure 3: timestamp zones in the command stream

```
◆ BLOOM_BEGIN
    mip0→UAV   down0   mip0→NPSR   mip1→UAV                         ┐ bloom down 0   ┐
◆ BLOOM_DOWN0_END                                                   ┘                │
    down1 … down4   (+ their barriers)                              ┐ bloom down rest│ bloom
◆ BLOOM_DOWN_END                                                    ┘                │
    mip3→UAV   up3 … up0   (+ their barriers)                       ┐ bloom up       │
◆ BLOOM_END                                                         ┘                ┘
    all mips→PSR
tonemap (composite)
```

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

### Measured

Four timestamp zones (ADR-0011, Figure 3): `bloom` around the whole
chain, including its barriers, and inside it `bloom down 0`,
`bloom down rest` (`down1`..`down4`) and `bloom up` (`up3`..`up0`). With
bloom off, the timestamps are still written, so the zones are empty
instead of holding stale slots.

The runs below were taken with `BLOOM_END` placed *after* the final
all-mips transition to `PIXEL_SHADER_RESOURCE`, so their `bloom` and
`bloom up` include that one barrier. In the bloom-off runs that barrier
was the only thing inside the zone, and it measured under 1 µs. On
2026-11-11 `BLOOM_END` moved in front of it (Figure 3), so later runs
measure the chain alone.

Setup: AMD Radeon RX 7800 XT, 1280×720, 1M particles, fixed camera,
about 19 000 frames per run, of which the first 1200 are skipped (`render`
settles over roughly 1000 frames). Values are p50 / p95 in µs:

| zone | Release | Debug |
|---|---:|---:|
| `bloom` | 25.0 / 32.7 | 35.1 / 41.7 |
| `bloom down 0` | 8.3 / 12.9 | 11.5 / 15.9 |
| `bloom down rest` | 6.0 / 8.5 | 8.8 / 10.5 |
| `bloom up` | 9.9 / 13.9 | 14.7 / 14.8 |

**The prediction was refuted.** It said `down0` would take about 75% of
the downsample time, because it reads three times as many pixels as all
later downsamples together (1/4 + 1/16 + … = 1/3). It takes 58% in Release
and 57% in Debug. As this section predicted for that outcome, the small
passes are bound by fixed per-dispatch and per-barrier cost, not by
bandwidth:

- `down rest` costs about 1.5 µs per dispatch, and `up` about 2.5 µs,
  regardless of size. At 720p, mip 3 is 60 thread groups and mip 4 is 15,
  on a GPU with 60 compute units. Each barrier drains the GPU before
  dispatches that cannot fill it again.
- `down0` moves about 4.6 MB (3.7 MB read, 0.9 MB written) in 8.3 µs,
  about 550 GB/s. That is close to the card's 624 GB/s DRAM figure, so
  `down0` behaves like a memory-bound pass. Timestamps cannot tell whether
  the source came from DRAM or from the 64 MB Infinity Cache.

**Total cost of the feature.** In Debug, `frame` rose by 49 µs at p50
while the control zone `render` moved by 0.1 µs. About 14 µs of that falls
outside the `bloom` zone. In those runs every bloom barrier was inside the
zone, so the most likely cause is the composite in the tonemap pass,
which reads one extra bilinear sample per full-resolution pixel. In Release, the control moved by
−4.9% (−17.5 µs) between the two runs. That is more than the effect being
measured, so the Release `frame` delta (−0.2%) is not resolvable. The
`bloom` zone (25 µs, about 0.15% of a 16.7 ms frame) is the reliable
Release number.

**Follow-up prediction, not yet run.** At 2560×1440 (4× the pixels),
`down0` should grow about 4× and `down rest` less than 1.5×. If
`down rest` also grows about 4×, the overhead explanation is wrong.
If it holds, the place to save time is the small mips: fewer levels, or a
single-pass downsampler such as AMD FidelityFX SPD. `down0` is not the
place.
