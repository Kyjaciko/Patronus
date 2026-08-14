# ADR 0002: Frames in flight vs backbuffer count

Status: Accepted
Date: 2026-08-11

## Context

Microsoft's D3D12HelloFrameBuffering sample uses a single `FrameCount`
constant for both the swapchain buffer count and how far the CPU may run
ahead, and a single `m_frameIndex` to index both render targets and command
allocators. The comment in that sample acknowledges these may differ but
the code does not separate them.

A command allocator owns the memory a command list records into. It may
only be reset once the GPU has finished executing the commands recorded
in it. If the CPU resets it early, the GPU reads freed memory: corruption
or a crash, often only on some vendors.

The naive fix — waiting for GPU idle every frame — serialises the two
processors. Frame time becomes CPU time plus GPU time instead of the
maximum of the two.

## Decision

`kBufferCount = 3` and `kFramesInFlight = 2` are separate constants because
they answer separate questions.

`kBufferCount` is a presentation question: one surface is being scanned out,
one is queued for the next flip, one is free to render into. Three keeps
`Present` from blocking on the compositor when a frame runs long. With two,
a single late frame at vsync costs a full refresh interval — 60 fps drops
to 30. The third surface buys slack, not throughput.

`kFramesInFlight` is a CPU question: how many frames' worth of commands may
be recorded ahead of GPU execution. It bounds input latency and sizes the
per-frame resources. A frame in flight is not a stored image — it is a
command list of a few kilobytes in system RAM. Raising this number
allocates no additional VRAM.

Two indices follow from this. `m_frameIndex` (0..kFramesInFlight-1) indexes
command allocators and fence values. `m_backBufferIndex` (0..kBufferCount-1)
indexes render targets and RTVs. They run out of phase and the pair
repeats every six frames. Sharing one index, as the sample does, means
either indexing a 2-element array with a value up to 2, or placing a
resource barrier on a different surface than the one being rendered to.

`BeginFrame` waits at two points, in this order:

1. The swapchain frame latency waitable object. This is a semaphore
   initialised to the maximum frame latency and decremented per wait;
   DXGI increments it when a slot in the present queue is retired. It
   bounds input latency by pacing the CPU to the display.

2. The fence value recorded for this slot. This is what makes resetting
   the allocator safe.

`m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex()` is called
only after both waits above, never before. For a waitable swap chain this
is a real ordering requirement, not a style choice: the returned index is
only meaningful once DXGI has retired a slot in the present queue, which
is exactly what the wait on the frame latency waitable object guarantees.
Querying it earlier risks reading an index DXGI has not finished retiring.

Both are required. The waitable object knows only about presents; it has
no knowledge of command allocators and cannot protect them. This becomes
unambiguous once a compute queue is added, since that queue never
presents at all.

The wait on the waitable comes first because it is typically the longer
of the two, so everything after it runs on the most recent input.

Fence values come from a single monotonic counter, `m_nextFenceValue`,
rather than the sample's `currentFenceValue + 1` pattern. The fence value
is a sequence number, not a frame counter; gaps are irrelevant. The only
requirement is that each signal is higher than the last. `WaitForGpu` also
consumes a value, so the per-slot sequence starts at 2.

One command allocator is created per frame in flight, not per backbuffer.
In the sample these counts coincided; they no longer do.

### Diagram

```
Naive (one allocator, wait for GPU idle):
CPU:  [record 1]---wait---[record 2]---wait---
GPU:  ---------[draw 1]-----------[draw 2]----
      frame time = CPU + GPU

Two allocators (this change):
CPU:  [record 1][record 2][record 3][record 4]
       alloc 0   alloc 1   alloc 0   alloc 1
GPU:  -----[draw 1][draw 2][draw 3][draw 4]
       frame time = max(CPU, GPU)
```

### Frame / slot / fence table

| frame | slot | `BeginFrame` waits on | `EndFrame` signals |
|------:|-----:|:----------------------|:--------------------|
|     0 |    0 | 0 (already passed)    | 2                    |
|     1 |    1 | 0 (already passed)    | 3                    |
|     2 |    0 | 2                     | 4                    |
|     3 |    1 | 3                     | 5                    |
|     4 |    0 | 4                     | 6                    |

At frame 2, slot 0 is reused, and the wait is on the value signalled when
that slot was last used.

## Consequences

CPU and GPU now overlap. Frame time is the maximum of the two rather than
their sum.

Not yet handled, deferred to a later milestone:

- swapchain resize (`m_renderTargets` and the RTVs become invalid)
- `DXGI_ERROR_DEVICE_REMOVED` from `Present` or `ExecuteCommandLists`
- fullscreen transitions
- sRGB: the backbuffer is `R8G8B8A8_UNORM` with a null RTV desc, so no
  gamma conversion is applied. Correct once real materials arrive.

## Recorded output

From a temporary `OutputDebugStringA` at the end of `BeginFrame`, now
commented out in the source. Debug build, vsync on, single triangle.

```
fence=1 slot=0 bb=0 completed=1
fence=2 slot=1 bb=1 completed=2
fence=3 slot=0 bb=2 completed=3
fence=4 slot=1 bb=0 completed=4
fence=5 slot=0 bb=1 completed=5
fence=6 slot=1 bb=2 completed=6
fence=7 slot=0 bb=0 completed=7
...
fence=27 slot=0 bb=2 completed=27
fence=28 slot=1 bb=0 completed=27
fence=29 slot=0 bb=1 completed=29
```

Reading this against the table above: the `fence` field is sampled inside
`BeginFrame`, before that same iteration's `EndFrame` runs — so it always
reports the value signalled at the end of the *previous* iteration, not
the one this iteration is about to produce. Numbering log lines by
iteration (line 1 = frame 0, line 2 = frame 1, ...): frame 0's own signal
(2, per the table) doesn't appear until line 2; line 1's `fence=1` is the
priming signal `WaitForGpu` issued at the end of `LoadAssets`, before the
render loop starts. `slot` and `bb` are current-state reads and line up
with the table's `frame` column directly; `fence` is one iteration behind.

`slot` cycles 0,1 while `bb` cycles 0,1,2. The pair repeats every six
frames, the lcm of 2 and 3. The indices are genuinely independent.

`completed` equals `fence` on almost every line. This is not evidence of
missing pipelining. With one triangle at vsync the GPU finishes in
microseconds while the frame budget is 16.6 ms, and the CPU is gated by
the frame latency waitable — by the time it reads the fence, the value
has long since been written. There is simply nothing to pipeline at this
workload.

Frame 28 is the exception: the GPU was one frame behind. That single line
is the case the fence exists for. Under real load it would be the norm.
