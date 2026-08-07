# 2026-08-07: D3D12 Fundamentals & CI Fixes

## D3D12 fundamentals: notes from the HelloTriangle sample

These are my own notes, written while studying Microsoft's
`D3D12HelloTriangle` sample from the
[`DirectX-Graphics-Samples`](https://github.com/microsoft/DirectX-Graphics-Samples)
repository (copied into `src/core/` for learning purposes), with explanations written myself while using Claude for extra explanations.

### Code flow

### Outermost loop of a D3D12 program

- Initialize
- Repeat
  - Update
  - Render
- Destroy

### Initialize

#### Initialize the pipeline

- Enable the debug layer
- **Create the device (`ID3D12Device`)**:
  Represents your connection to the GPU.

  It's the primary factory object to create almost everything else the API
  (DirectX) needs. It's a creation and query (GPU capabilities/features)
  object. At creation time, the GPU vendor's user-mode driver DLL (NVIDIA's,
  AMD's, or Intel's) gets loaded into our process and when using it it jumps
  into the driver code now running in our own process.

- **Create the command queue (`ID3D12CommandQueue`)**:
  It's the pipe through which our app submits GPU commands for execution.

  Its a FIFO queue where which each element of the list is a "list of
  commands". Within these lists the commands within a single command list
  are executed in order but the lists relative to each other are not
  necessarily run in order, can be pipelined/overlapped or reordered. One of
  D3D12's big low-level features versus D3D11: you can have a graphics queue
  and a compute queue (and a copy queue) executing simultaneously on
  hardware that supports it, letting you overlap work like async compute.
  To ensure synchronization between these queues and the CPU fences can be
  used to wait until the required list is finished.

- **Create the swap chain (`IDXGISwapChain3`)**:
  It's the object that manages the set of buffers our app renders into and
  presents to the screen.

  It's not part of DirectX itself, it lives in DXGI because presentation is
  a windowing/OS-level concern, not a rendering concern.
  Instead of rendering directly to the buffer the screen is reading from
  (which causes tearing/flicker), you render to an offscreen buffer (back
  buffer), then swap it with the front buffer once it's done.
  Where the Back buffer(s) is what you render into. The front buffer is
  what's currently being read. The swap chain cycles the buffers between
  these roles.

  > **Note:** With the old BitBlt-style swap effects (DISCARD/SEQUENTIAL),
  > buffers were literally swapped (pointer flip) or copied. But D3D12
  > requires flip-model presentation (FLIP_DISCARD/FLIP_SEQUENTIAL), where
  > the back buffer is handed off to the DWM (Desktop Window Manager) rather
  > than truly "swapped" with the front buffer in the old sense. With flip
  > model, you often don't have direct access to or ownership of a single
  > front buffer the way you did in older APIs, the DWM composites and
  > displays what you present, and it's less of a distinct D3D-managed
  > object and more of an OS-level compositor concern. This is important to
  > know for example transparent windows.

- **Create a render target view (RTV) descriptor heap (`ID3D12DescriptorHeap`)**:
  It's a block of memory that holds descriptors for render targets.

  Desciptors: In D3D12, resources (textures, buffers) are "raw" GPU memory,
  the GPU doesn't inherently know how to use them. A descriptor is a
  lightweight object that describes how to view/use a resource: its format,
  dimensions, mip levels, etc. There are several descriptor types: CBV
  (constant buffer), SRV (shader resource), UAV (unordered access), RTV
  (render target), DSV (depth-stencil), and samplers.

  RTV: An RTV describes a resource as a color render target, i.e. a surface
  the pipeline can write pixel color output to (like a swap chain or an
  offscreen texture)

- **Create frame resources (a render target view for each frame; `ID3D12Resource`)**:
  It's a set of per-frame GPU resources, most importantly the fence value, a
  command allocator and any constant buffers holding data that changes
  every frame (world matrix of every object, view matrix, projection
  matrix, etc.), duplicated once per swap chain buffer, so the CPU can
  safely prepare a new frame's data/commands while the GPU is still working
  on a previous frame using its own separate copy.

- **Create a command allocator (`ID3D12CommandAllocator`)**:
  It's the backing memory that stores GPU commands recorded into a command
  list; the command list itself is just the recording interface, while the
  allocator owns the actual memory, so it can't be reset for reuse until the
  GPU has finished executing everything that was recorded into it.

#### Initialize the assets

- **Create an empty root signature (`ID3D12RootSignature`)**:
  It defines the interface between our shader code and the resources
  (buffers, textures, samplers, constants) it expects to receive.
  Think of it as a "function signature" for the GPU pipeline, it declares
  what kinds of resources will be bound and where, without actually binding
  the data itself.

- Compile the shaders
- **Create the vertex input layout (`D3D12_INPUT_ELEMENT_DESC`)**:
  It describes how the raw bytes in your vertex buffer(s) should be
  interpreted and mapped to the inputs of your vertex shader.
  It's part of the Input Assembler (IA) stage configuration, and it's baked
  into a Pipeline State Object (PSO) at creation time.

- **Create a pipeline state object description, then create the object (`ID3D12PipelineState`)**:
  It describes the complete GPU state needed to execute a draw or dispatch
  call, shaders, blend/rasterizer/depth-stencil settings, input layout,
  render target formats, and primitive topology, all bundled together and
  validated/compiled up front.

  > **Note:** It's one of the defining features that distinguishes D3D12
  > from D3D11.

- **Create the command list (`ID3D12GraphicsCommandList`)**
- Close the command list
- **Create and load the vertex buffer(s) (`ID3D12Resource`)**:
  It's a GPU buffer resource that holds the per-vertex data used as input to
  the vertex shader stage of the graphics pipeline, things like: position,
  normal, texture coordinates, color, etc., for each vertex of the geometry
  you want to draw.

- **Create the vertex buffer view(s) (`D3D12_VERTEX_BUFFER_VIEW`)**:
  It's a small descriptor structure that tells the GPU how to interpret a
  region of memory as an array of vertices when it's bound as an input to
  the Input Assembler (IA) stage.

- **Create a fence (`ID3D12Fence`)**:
  It's a synchronization primitive used to track and coordinate work
  between the CPU and GPU (and somtimes between different GPU queues).
  Basically it let's the CPU now the GPU is done with the current frame
  resource so the CPU can write data back into it.

  > **Note:** It's essentially a 64-bit integer counter that lives in
  > memory shared between CPU and GPU, plus a bit of machinery to let you
  > wait on it.

- **Create an event handle (`HANDLE`)**:
  It's a Windows OS synchronization primitive that DirectX uses to let the
  CPU know when the GPU has finished a piece of work (most commonly in
  combination with fences). Instead of constantly polling the fence value,
  that wastes CPU cycles, the fence signals the event once it reaches the
  target value, waking the CPU thread. Basically, it bridges GPU completion
  status into something the CPU thread can block on efficiently rather than
  busy-waiting.

  > **Note:** It's a plain Win32 OS Object. Nothing DirectX-specific about
  > it. DirectX just gives fences the ability to trigger it.

- Wait for the GPU to finish

### Update

- Update everything that should change since the last frame
  - Modify the constant, vertex, index buffers, and everything else, as
    necessary

### Render

- Populate the command list.
  - Reset the command list allocator (re-use the memory that is associated
    with the command allocator)
  - Reset the command list
  - Set the graphics root signature
  - Set the viewport and scissor rectangles

    Viewport: It defines how normalized device coordinates (NDC) get mapped
    onto your actual render target (the screen or texture you're drawing
    into). Example: Render target is allocated at max size (say
    1920×1080), but you shrink the viewport to e.g. 1280×720 under heavy
    GPU load, meaning you only render into the top-left 1280×720 region of
    that texture. The other ~56% of pixels in the texture are just
    untouched/garbage data from a previous frame. Then upscale only that
    1280×720 sub-rectangle.

    Scissor Rectangle: It defines a clipping region, any pixels generated
    by rasterization outside this rectangle are discarded and never reach
    the pixel shader/output merger.

    > **Note:** Scissor rectangle is a good use-case for example when you
    > know only a small region of the screen changed since last frame (say,
    > a blinking cursor). You don't want to re-render the whole scene, you
    > set the scissor rect to just that small dirty region, and every draw
    > call that frame is clipped to it, skipping fragment shading work
    > everywhere else.

  - Set a resource barrier, indicating the back buffer is to be used as a
    render target

    It's a synchronization mechanism that tells the GPU driver that a
    resource is transitioning from one usage state to another.

    > **Note:** This needed since GPU resources (textures, buffers) can be
    > used in different ways at different times: as a render target, a
    > shader input, a copy source/destination, etc. Internally, the GPU may
    > lay out memory or cache the resource differently depending on how
    > it's being used, for performance reasons. If you try to use a
    > resource in a new way without telling the GPU to transition it, you
    > can get undefined behavior.

  - Record commands into the command list
  - Indicate the back buffer will be used to present after the command list
    has executed (another call to set a resource barrier)
  - Close the command list to further recording

- Execute the command list
- Present the frame
- Wait for the GPU to finish

### Destroy

- Wait for the GPU to finish (it's the final check on the fence)
- Close the event handle

## CI fixes

Pushing the scaffold and the HelloTriangle learning commit turned up two CI
failures, both root-caused before fixing (not guessed):

1. **Build job: CMake generator not found.** GitHub migrated the
   `windows-latest` runner label to Windows Server 2025 with Visual Studio
   2026 (rolled out June 8–15, 2026). This project's CMake presets hardcode
   the `"Visual Studio 17 2022"` generator, which doesn't exist on that
   image. Fixed by pinning `runs-on: windows-2022` in `.github/workflows/ci.yml`
   for both jobs, keeps CI on the same toolchain as local dev (VS2022
   Community) rather than chasing VS2026.
2. **format-check job: clang-format version skew.** The same image swap
   changed which `clang-format` was first on `PATH`, disagreeing with the
   VS2022-bundled 19.1.5 the source files were formatted against. Resolved
   as a side effect of the `windows-2022` pin above.