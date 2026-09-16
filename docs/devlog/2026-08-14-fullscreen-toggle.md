# 2026-08-14: Tearing-Aware Fullscreen Toggle

*Notes on the implementation, drafted with Claude's help from the code and
commit history — same disclosure as the previous entry.*

## What changed

`Space` toggles the window between windowed and fullscreen
(`D3D12HelloTriangle::OnKeyDown`, `VK_SPACE`). The actual transition takes
one of two paths depending on whether the adapter supports tearing:

- **Tearing supported** (`m_tearingSupport == true`): a borderless
  fullscreen *window* is used instead of the swap chain's own exclusive
  fullscreen mode (`Win32Application::ToggleFullscreenWindow`) — resize the
  window to cover the monitor, no `IDXGISwapChain::SetFullscreenState`
  call at all.
- **No tearing support**: fall back to the swap chain's real exclusive
  fullscreen via `SetFullscreenState(!fullscreen_state, nullptr)`.

Either path ends up changing the window size, which fires a `WM_SIZE`
message and lands in `OnSizeChanged`, which is where the swap chain is
actually resized (`ResizeBuffers`) and the RTVs are recreated.

## Why two paths, not one

`DXGI_PRESENT_ALLOW_TEARING` — needed for uncapped/VSync-off presentation
without tearing artifacts being *worse* than intended — is documented by
Microsoft as mutually exclusive with legit exclusive fullscreen entered
through `SetFullscreenState`. `Present` is only allowed to pass that flag
while the swap chain is in windowed mode
(`m_tearingSupport && m_windowedMode` gate in `OnRender`, see
`DXGI_PRESENT_ALLOW_TEARING` usage). So on hardware that supports tearing,
staying in a borderless *window* the whole time — rather than actually
entering exclusive fullscreen — is what lets tearing-based presentation
keep working after the toggle. On hardware without tearing support there's
no such conflict, so real exclusive fullscreen is used instead, which is
generally lower-latency than borderless when it's available.

## Resize coordination

`OnSizeChanged` has to do a specific sequence, in order, to stay valid
against the frames-in-flight scheme from [ADR-0002](../adr/0002-frames-in-flight.md):

1. `WaitForGpu()` — flush all in-flight work first. The render target
   views about to be released, and the fence values about to be reset,
   must not still be referenced by anything the GPU hasn't finished yet.
2. Release all `kBufferCount` render target resources (`m_renderTargets[n].Reset()`) —
   `ResizeBuffers` requires every outstanding reference to the current
   back buffers to be dropped first, or it fails.
3. Reset every frame-in-flight's fence value to the fence value of the
   frame that was just flushed — the old per-slot fence values are for
   buffers that no longer exist at the new size, so they'd otherwise
   compare as "already reached" against a fence the GPU hasn't actually
   signalled to that value at the new size.
4. `ResizeBuffers(kBufferCount, m_width, m_height, ...)`, then re-fetch
   `GetCurrentBackBufferIndex()` — resizing can hand back a different
   starting back buffer index than the one before the resize.
5. Re-create one RTV per back buffer, and resize the viewport/scissor
   rect to match.

Skipping the wait in step 1, or resizing before dropping the RTV
references in step 2, are both documented ways to make `ResizeBuffers`
fail outright — this ordering isn't arbitrary.

## Known gap

`OnDestroy` unconditionally forces `SetFullscreenState(FALSE, nullptr)`
before exiting only "if (!m_tearingSupport)" — i.e. only when the app
could have entered *real* exclusive fullscreen. Leaving exclusive
fullscreen active when the process dies is a well-known way to leave the
display driver in a bad state, so this exit-time cleanup matters, but
there's a TODO already in the code next to it: if `SetFullscreenState`
or `Present`/`ResizeBuffers` fail elsewhere, `COM_ERROR_IF_FAILED`
terminates the process immediately (`exit(-1)`) without running this
cleanup path — a real (if narrow) way to still leave fullscreen state
dangling. Not fixed yet.
