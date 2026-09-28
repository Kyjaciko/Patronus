# ADR 0012: HDR10 and scRGB display output

Status: Accepted
Date: 2026-09-23 (revised 2026-09-28)

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

The scene renders into ADR-0003's `R11G11B10_FLOAT` target with one
pipeline state per pass, whatever the output mode. The scene shaders no
longer know which display they end up on. Only the tonemap pass, and
anything drawn after it, needs a pipeline state per output mode.

The tonemap pass keeps one shader with one pipeline state per output mode,
differing only in the final encode, and always produces **absolute
luminance in nits** before that encode. Three parameters drive it, all
exposed in the debug UI:

- `exposure`, in stops. The shader converts it with `exp2` and applies it
  before the curve.
- `paper_white_nits`, the scale from relative scene units to nits. The
  default is 203, the graphics-white level in ITU-R BT.2408. It is a
  choice, not a measurement: no API reports it.
- `peak_nits`, the ceiling the curve rolls off towards. The default is
  `MaxLuminance`, read after the output has been queried. It stays a
  slider because EDID values are not always accurate.

The operator is Reinhard with its asymptote moved from 1.0 to the display's
headroom:

```
y = x / (1 + x / m)        m = peak_nits / paper_white_nits
```

For small `x`, `y ≈ x`, so shadows and midtones land on the same nits as a
linear mapping would put them. For large `x`, `y` approaches `m`, so after
scaling by `paper_white_nits` the output never exceeds `peak_nits`. SDR
uses the same function with `m = 1`, which is plain Reinhard, and writes
the result linearly for the `_SRGB` view to encode. The HDR modes take
`y × paper_white_nits` as nits. scRGB then divides by 80, because scRGB
defines 1.0 as 80 nits. HDR10 applies the BT.2087 Rec.709 to Rec.2020
matrix, divides by 10000 and applies the SMPTE ST 2084 PQ curve.

Because Reinhard bends from zero, scene 1.0 lands at 140 nits rather than
at `paper_white_nits`. Paper white is the scale of the mapping, not the
exact level of scene 1.0. That is accepted for now; see Future work.

Extended Reinhard with `L_white = m` was tried first and rejected. Its
white parameter is the scene value that maps to output 1.0, not a ceiling.
Scene 2.24 landed on paper white (203 nits), and above that the curve kept
rising without bound. Scene 7.7 already reached the panel's 455 nits, so
the display clipped the whole spell core (8 to 20) to one flat level.

`SetHDRMetaData` is not called. Microsoft's guidance is that DWM handles
windowed presentation itself, and the mastering metadata mattered mainly
for fullscreen-exclusive.

The debug UI renders into its own SDR target and is composited by the
tonemap pass at `paper_white_nits`, superseding ADR-0003's note that ImGui
draws straight into the swapchain. **Deferred:** not implemented yet. Until
it is, ImGui draws into the swapchain with its pipeline state rebuilt per
mode. Its sRGB-encoded colours are read as linear scRGB in one mode and as
PQ code values in the other, so the UI is wrong in both HDR modes and is
the one visible difference between them.

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

## Validation

The scRGB/HDR10 acceptance test passed on 2026-09-28 for the scene. The UI
is excluded until it has its own target. Before it passed, the test caught
two bugs that neither mode would have revealed on its own:

- `peak_nits` was set from `DisplayOutput` in the class's member
  initialiser, before the output had been queried, so it was 0. The curve
  divided by zero: lit pixels became `inf` and black ones `NaN`. HDR10 hid
  most of it, because `saturate` in the PQ encode turns `inf` into 1.0 and
  `NaN` into 0. scRGB passed both straight to DWM.
- `exposure` is a slider in stops but was multiplied in as a linear
  factor. At 0 EV the image went black, and negative EV produced negative
  colours. The HDR10 path clamps those in `saturate`. scRGB treats negative
  values as valid out-of-gamut colour, so only that mode showed them.

Two further checks passed:

- Raising `peak_nits` makes the core brighter while UI white and the dark
  areas barely change.
- With `peak_nits` equal to `paper_white_nits` (`m = 1`) the HDR image has
  the same shape as SDR. The brightness still differs, because DWM shows an
  SDR swapchain at the Windows "SDR content brightness" level, not at
  `paper_white_nits`. Moving both sliders together until the two modes
  match measures the desktop's SDR white level on this machine (open
  question 2).

## Why HDR does not look dramatically different here

Switching between SDR and the HDR modes shows a colour difference but not
the jump in contrast seen in HDR films or AAA games. On this panel with
this content, that is expected. The table puts numbers on it, with
exposure folded into the scene value and the Windows SDR white level
assumed to equal `paper_white_nits` (203):

| Scene value | SDR | HDR (`m` = 2.24) | HDR / SDR |
|---|---|---|---|
| 0.18 (mid grey) | 31 nits | 34 nits | 1.09x |
| 1.0 | 102 nits | 140 nits | 1.38x |
| 4 | 162 nits | 292 nits | 1.80x |
| 12 (spell core) | 187 nits | 384 nits | 2.05x |
| 20 | 193 nits | 410 nits | 2.12x |

Shadows and midtones are within 10% of each other, and the core is about
one stop brighter. That one stop is the whole difference. The reasons:

- **The headroom is small.** 455 nits against a 203-nit paper white is 1.2
  stops. With the same paper white, a 1000-nit panel would put the core at
  709 nits, 3.8x SDR. Films are mastered at 1000 to 4000 nits. Automatic
  brightness limiting takes more away as the lit area grows.
- **Black is not what HDR adds on this panel.** The OLED shows true black in
  SDR mode too. The curve deliberately leaves everything below paper white
  alone, so the only place the modes can differ is above it.
- **The contrast in films and games comes mostly from the curve and the
  grade, not from HDR.** A filmic curve has a toe that pushes shadows down
  and a steep midsection, and the colourist grades on top of it. Reinhard
  has no toe and the flattest midtones of the common operators, so the
  image looks flat in every mode. The missing punch is the operator, not
  the output path.
- **The content barely uses the headroom.** Only pixels above paper white
  can look different, and the current scene has few of them. There is no
  bloom yet (M1). Film and game content places speculars, fire and the sun
  there deliberately.
- **There is no gamut gain.** Everything is authored in Rec.709. HDR10's
  Rec.2020 container carries no colours the SDR path cannot also show.

The colour difference that is visible comes from applying the curve per
channel. A blue core at `(0.5, 2, 12)` becomes `(0.33, 0.67, 0.92)` in SDR:
blue is 2.8x red instead of 24x, and the colour washes towards white. In
HDR it becomes `(0.41, 1.06, 1.89)`, blue at 4.6x red, so noticeably more
of the saturation survives. The HDR modes are more saturated because they
compress less, not because they show new colours.

## Future work

In rough order of how much each would change the visible result:

1. **A curve with a toe, a linear section and a shoulder.** Uchimura's
   Gran Turismo operator (CEDEC 2017, "HDR Theory and Practice") is the
   boring next step because its parameters map directly onto this ADR:
   maximum brightness is `m`, a linear section can place scene 1.0 exactly
   on paper white, and a toe adds shadow contrast in SDR and HDR alike.
   Hable (Uncharted 2), ACES and AgX are the alternatives. ACES needs its
   output range reinterpreted for a 455-nit ceiling. This is the change
   that addresses the flat look, and it improves both modes.
2. **Tonemap luminance or max(R, G, B) instead of each channel.** SDR and
   HDR would then differ in brightness but keep the same hue and
   saturation. Very bright saturated colours still need a deliberate path
   to white or they look unnatural, so this is a look decision as much as
   a technical one.
3. **Content that uses the headroom.** Bloom in M1, a core authored well
   above paper white, and bright areas kept small because of automatic
   brightness limiting.
4. **Trade paper white for headroom.** A paper white of 120 to 150 nits
   buys up to about 0.8 stops more room above it, at the cost of a UI that
   looks dim beside other windows. Games solve this with a calibration
   screen: a pattern for the peak, a slider for paper white.
5. **Show the difference without an HDR screenshot.** A test strip of
   scene values from 0 to 20, and a false-colour view of output nits, make
   the SDR clip against the HDR roll-off visible in an ordinary capture
   for the README.

## Open questions

1. ~~Does HDR output belong after M1 or inside it?~~ **Resolved:** done as
   its own step before the rest of M1. The HDR target and the tonemap pass
   that M1 needs now exist; the filmic operator (Future work 1) and bloom
   remain M1 work.
2. `paper_white_nits` default: **partly resolved.** 203 per BT.2408. Still
   to do: measure the desktop's SDR white level with the method under
   Validation, and decide whether the default should match it instead.
3. ~~Which tonemap operator maps to nits?~~ **Resolved:** Reinhard with its
   asymptote at `m`; extended Reinhard rejected (see Decision). Next
   candidate in Future work 1.
4. Should the debug UI composite at `paper_white_nits` or at the OS SDR
   white level from `DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL`? The
   second matches the desktop exactly and adds a Win32 CCD dependency that
   nothing else in the project needs. **Deferred** with the UI target.
