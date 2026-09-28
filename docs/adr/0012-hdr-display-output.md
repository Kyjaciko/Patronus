# ADR 0012: HDR10 and scRGB display output

Status: Proposed
Date: 2026-09-23

## Context

ADR-0003 renders the spell into an `R11G11B10_FLOAT` target and tonemaps
into an 8-bit swapchain. Everything the HDR pipeline is built for — a core
at intensity 8 to 20, white-hot sparks, a bloom fed by values far above
1.0 — is crushed back into the SDR range in the last pass. On a display
that is already in HDR mode, that last step throws away the one thing the
rest of the pipeline exists to produce.

The dev machine's panel, queried through `IDXGIOutput6::GetDesc1` on
2026-09-23:

| Field | Value |
|---|---|
| `ColorSpace` | `RGB_FULL_G2084_NONE_P2020` (12), `G22_NONE_P709` (0) with HDR off |
| `BitsPerColor` | 10 |
| `MinLuminance` | 0.000 nits |
| `MaxLuminance` | 455.5 nits |
| `MaxFullFrameLuminance` | 253.8 nits |
| Primaries R / G / B | (0.6846, 0.3027) / (0.2344, 0.7275) / (0.1475, 0.0488) |
| White point | (0.3135, 0.3291), near D65 |

Four things follow from those numbers, and they shape the decision more
than any API detail:

- **Only `ColorSpace` tracks the Windows HDR toggle.** Every other field,
  including `MaxLuminance` and `BitsPerColor`, reads the same with HDR
  switched off. A capability check written against the luminance values
  would report HDR on an SDR desktop.
- **The headroom is small.** With paper white at 200 nits, peak white is
  2.28x that — about 1.2 stops above paper white, and full-frame white is
  only 1.27x, about 0.3 stops. The tonemapper's job on this panel is to
  compress a wide scene range into a slightly wider display range, not to
  let bright values run free.
- **Area costs brightness.** `MaxFullFrameLuminance` is 56% of
  `MaxLuminance` because the panel dims as the lit area grows (OLED
  automatic brightness limiting). A small bright core reads far brighter
  than a full-screen flash, and a wide bloom spends its energy raising the
  average picture level, which dims the frame it is trying to brighten.
- **`MinLuminance` is 0.** True black is available, so the contrast — not
  the peak — is where this panel's advantage actually sits.

The panel's primaries are wider than Rec.709 and narrower than Rec.2020.
They are informational here: HDR10 always encodes into the Rec.2020
container and the display gamut-maps to its own primaries internally.

Alternatives considered:

- **Stay SDR-only.** M1 as planned, no extra pass variants, no HDR-specific
  UI problem. The pipeline's whole premise stays invisible on the hardware
  that could show it.
- **HDR10 only.** One output mode, but the shader path (nits, Rec.2020
  matrix, PQ encode) has three places to be wrong and no reference to check
  against.
- **scRGB only.** The shader path is a scale and a linear write, so it is
  almost impossible to get wrong. It costs 8 bytes per pixel in the
  swapchain against 4, and DWM performs the PQ encode.
- **Both, with SDR unchanged.** scRGB exists mainly to validate HDR10: the
  two modes must produce an identical image, which turns the PQ and matrix
  maths into a testable claim rather than a hope.

## Decision

Add a runtime-selectable output mode to the presentation path, with SDR
unchanged from ADR-0003 so that M1 is unaffected.

A `Display` class owns the query side: it finds the `IDXGIOutput6` behind
the window's `HMONITOR`, reads `DXGI_OUTPUT_DESC1`, and reports HDR
availability **from `ColorSpace` alone**, keeping the luminance values as
tonemap inputs. It re-queries on `WM_DISPLAYCHANGE` and on window moves.

Three output modes, each a fixed pairing of swapchain format, RTV format
and colour space:

| Mode | Swapchain format | RTV format | Colour space |
|---|---|---|---|
| SDR | `R8G8B8A8_UNORM` | `R8G8B8A8_UNORM_SRGB` | `RGB_FULL_G22_NONE_P709` |
| HDR10 | `R10G10B10A2_UNORM` | same | `RGB_FULL_G2084_NONE_P2020` |
| scRGB | `R16G16B16A16_FLOAT` | same | `RGB_FULL_G10_NONE_P709` |

The SDR row is the only one with a separate RTV format: the hardware
applies the sRGB transfer function on write. The other two have no sRGB
variant, so the tonemap shader writes the final encoded value itself.

Switching modes is a format change, not just a colour-space call: wait for
the GPU, release every back-buffer reference, `ResizeBuffers` with the new
format and the original flags, rebuild the RTVs, then
`CheckColorSpaceSupport` and `SetColorSpace1`. That order is forced —
colour-space support is reported against the current buffer format.
`CheckColorSpaceSupport` is a compatibility check, not an HDR check; under
DWM it can report HDR10 as presentable on an SDR desktop.

The tonemap pass keeps one shader with one pipeline state per output mode,
differing only in the final encode, and always produces **absolute
luminance in nits** before that encode. Two parameters drive it, both
exposed in the debug UI: `paper_white_nits` (what scene 1.0 maps to,
defaulting to 200 to match Windows' SDR white level) and `peak_nits` (from
`MaxLuminance`, where the highlight shoulder lands). The Rec.709 to
Rec.2020 matrix is BT.2087; the PQ transfer function is SMPTE ST 2084.

`SetHDRMetaData` is not called. Microsoft's guidance is that DWM handles
windowed presentation itself, and the mastering metadata mattered mainly
for fullscreen-exclusive.

The debug UI renders into its own SDR target and is composited by the
tonemap pass at `paper_white_nits`, superseding ADR-0003's note that ImGui
draws straight into the swapchain.

Mode selection is manual, from a combo box, with the HDR modes disabled
while the output reports SDR. Losing HDR mid-session updates the reported
state and leaves the current mode alone.

## Consequences

- Acceptance test: scRGB and HDR10 must look identical. Any visible
  difference is a bug in the Rec.2020 matrix or the PQ encode, and the
  cheap mode is the reference for the cheap-to-get-wrong one.
- On this panel, HDR buys roughly 1.2 stops above paper white, not the
  several stops the phrase suggests. A tonemap curve tuned to "let the core
  blow out" will clip at 455 nits and look worse than the SDR version, so
  the operator has to roll off toward `peak_nits`, which is a curve the SDR
  path does not need. The SDR and HDR looks will not match, and that is the
  point rather than a defect.
- The impact flash and any wide bloom trigger automatic brightness
  limiting, so raising their intensity past a point dims the whole frame.
  Highlights read brighter when they stay small. This is a real constraint
  on the M4 layer tuning and is worth capturing as a before/after in the
  writeup.
- The mode switch is the swapchain-recreate path with a format change,
  which the existing window-resize path mostly already performs. It is also
  the only place in the renderer that drops and rebuilds back-buffer
  references, so a stale reference shows up as `DXGI_ERROR_INVALID_CALL` at
  exactly one call site.
- One extra tonemap pipeline state per output mode, built up front. Every
  pipeline state that targets the swapchain bakes in its RTV format, so
  anything new drawn after the tonemap has to be built per mode too — a
  standing cost on any future post-tonemap pass.
- PQ over 0 to 10000 nits quantised to 10 bits leaves visible steps in
  smooth dark gradients, which is where an OLED with true black is most
  revealing and where bloom falloff lives. Dithering before the quantise is
  expected to be necessary, and if so it belongs in the same pass.
- HDR output cannot be shown in a normal screenshot: a standard capture is
  a tonemapped SDR version. README and video material need an HDR capture
  (Game Bar produces `.jxr`) or an honest note that the SDR still
  understates the result.
- Auto HDR has to stay off for the executable, or the frame on screen is
  not the frame the renderer produced.

## Open questions

1. Does HDR output belong after M1 as its own step, or inside M1? The
   tonemapper has to exist before any of this can be validated, which
   argues for after.
2. `paper_white_nits` default: 200 matches the desktop, so the UI sits at
   the brightness the rest of Windows uses, and leaves 1.2 stops of
   headroom. A lower value buys headroom and makes the UI look dim beside
   other windows. Settle it by comparing against Windows UI white on the
   panel.
3. Which tonemap operator maps to nits? An extended Reinhard with its white
   point at `peak_nits` is the boring starting point; ACES needs its output
   range reinterpreted rather than used as-is.
4. Should the debug UI composite at `paper_white_nits` or at the OS SDR
   white level from `DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL`? The
   second matches the desktop exactly and adds a Win32 CCD dependency that
   nothing else in the project needs.
