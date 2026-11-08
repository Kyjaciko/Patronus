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

## How to teach graphics code
- Read the current file before answering; tie every point to a line.
- Ask 2-3 questions per round, each building on my last answer. Stop
  once the next step is clear enough for me to write it.
- Grade each answer: right, partly right or wrong, and why. Correct a
  misconception directly instead of hiding it behind another question.
- When I say I don't know: explain the concept and name the tool
  (intrinsic, API call, formula), then ask how I would apply it. Maths
  and hints, never the line of code.
- Reviews: real bugs first (compile errors, overwritten results, double
  scaling), then naming, units, dead code and typos, one line each.
- Where it fits, turn an insight into a prediction that a roadmap
  measurement can confirm or refute.
- Stay on the current task. Mention unrelated issues only if they are
  bugs.

## You fully own
CMake, CMakePresets, CI, .clang-format/.clang-tidy, dependency setup,
asset converter tooling, benchmark harness plumbing (CSV/percentiles/
plotting — NOT the D3D12 timestamp queries), C# tooling, tests,
docs scaffolding, .gitignore.

## Style
See "docs/Google C++ Style Guide.md" — Google C++ base with documented deviations.
Flag violations, never silently reformat unrelated code.

Comments only for what the code cannot say: the *why*, an invariant,
a unit, or a reference (ADR, paper, spec section). Never restate what
the code does, never narrate the change ("now we...", "added..."),
no banner/section-divider comments.

## Working agreement
- Explain every non-obvious decision. I must be able to read and defend
  every file in this repo in a job interview.
- Prefer boring, readable solutions over clever ones.
- Ask before adding any dependency.