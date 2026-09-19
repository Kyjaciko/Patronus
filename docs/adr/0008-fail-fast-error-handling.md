# ADR 0008: Fail-fast HRESULT handling instead of exceptions

Status: Accepted (retroactive; decision made 2026-08-09, written 2026-09-17)
Date: 2026-08-09

## Context

The vendored `D3D12HelloTriangle` sample reports every failed D3D12 call
through `ThrowIfFailed`, which throws an `HrException`. `docs/STYLE.md`
rules exceptions out for this project (unpredictable unwind cost in a
frame loop, `HRESULT`-based API, dependencies built with exceptions off).
The sample also swallowed the useful information: an uncaught
`HrException` surfaces as an anonymous `0xE06D7363` with no HRESULT text
and no call site, which is what made the first-run crash (see the
2026-08-07 devlog addendum) take two days to identify.

Alternatives considered:

- Keep `ThrowIfFailed` and catch at `main`. Violates the style rule and
  still needs a handler to print anything useful.
- Return `HRESULT` from every function and propagate. Correct for a
  library; for a demo whose only sensible response to a failed device or
  swapchain call is to stop, it is a lot of plumbing for no behaviour.
- Fail fast: report and terminate at the call site.

## Decision

`COM_ERROR_IF_FAILED(hr, msg)` (`src/core/COMException.h`) wraps every
D3D12/DXGI/Win32 call that can fail. On failure it formats the HRESULT's
system message (`_com_error`), the caller's message, `__FILE__`,
`__FUNCTION__` and `__LINE__`, shows them in a `MessageBoxW`, and calls
`exit(-1)`. Nothing is thrown; the class name `COMException` is
historical and the type is only a message builder.

The macro is a `do { } while (0)` so it composes with `if`/`else` without
a dangling-else hazard, and it evaluates `hr` once.

## Consequences

- Every failure is loud and locates itself. This is the property that
  found the Agility SDK version mismatch.
- `exit(-1)` runs `atexit` handlers and static destructors but does not
  unwind the stack: no `WaitForGpu`, no `SetFullscreenState(FALSE)`, no
  `ComPtr` releases. For a debug demo this is acceptable; the exclusive
  fullscreen case is the one known ugly consequence (recorded in the
  2026-08-14 devlog).
- Device removal (`DXGI_ERROR_DEVICE_REMOVED`) is treated as fatal. A
  shipping renderer would recreate the device; this project will not.
- A modal message box on a headless machine (CI) hangs the process.
  The macro must not be used in anything CI executes. The smoke test in
  `src/rhi/smoketest` prints and returns an exit code instead, on purpose.
- `StringHelper::StringToWide` widens bytes one-to-one. It is used only
  on `__FILE__`/`__FUNCTION__` and ASCII messages, which is fine; it is
  not a UTF-8 conversion and should not be used as one.
- When the code migrates out of the sample into `src/rhi/`, the same
  policy applies. If a non-fatal path is ever needed (for example a
  failed optional feature query), it should be an explicit `if (FAILED)`
  branch, not a softer variant of this macro.
