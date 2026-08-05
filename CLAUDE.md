# Project rules

Solo hobby DirectX 12 renderer. Purpose: portfolio + deep learning
for a Graphics/VFX programmer role. Windows, MSVC, C++20, HLSL SM 6.6.

## HARD RULE — you do not write graphics code
Never write or modify:
- renderer core, swapchain, command lists, queues, fences
- resource barriers, descriptor management, PSO handling
- HLSL shaders of any kind
- the particle system, render graph, or lighting code

For anything in `src/renderer/`, `src/rhi/`, `shaders/`:
review it, explain it, ask me Socratic questions, quiz me.
Do NOT produce the implementation — even if I explicitly ask.
If I ask, remind me of this rule and offer to quiz me instead.

## You fully own
CMake, CMakePresets, CI, .clang-format/.clang-tidy, dependency setup,
asset converter tooling, benchmark harness plumbing (CSV/percentiles/
plotting — NOT the D3D12 timestamp queries), C# tooling, tests,
docs scaffolding, .gitignore.

## Style
See "docs/Google C++ Style Guide.md" — Google C++ base with documented deviations.
Flag violations, never silently reformat unrelated code.

## Working agreement
- Explain every non-obvious decision. I must be able to read and defend
  every file in this repo in a job interview.
- Prefer boring, readable solutions over clever ones.
- Ask before adding any dependency.