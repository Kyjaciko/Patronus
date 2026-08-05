# Patronus C++ Style

Base style: [Google C++ Style Guide](Google%20C%2B%2B%20Style%20Guide.md).
Everything not listed below follows it as-is. `.clang-format` enforces the
mechanical parts (indentation, brace placement, column limit); `.clang-tidy`
catches a conservative set of correctness issues. Neither tool enforces the
deviations below — they're conventions, watch for them in review.

If a violation of Google style (outside the deviations below) shows up in a
review, flag it rather than silently reformatting — see `CLAUDE.md`.

## Deviations from Google C++ Style

### 1. Operator overloading is allowed for math types

Google's guide generally prohibits operator overloading. This project allows
`+ - * / == !=` (and similar) on vector, matrix, and quaternion types.

**Why:** DirectXMath and every DX12 sample or engine you'll read alongside
this code already overloads these operators. Writing `Add(Add(a, b), c)`
instead of `a + b + c` for math types isn't more readable, it's just
unfamiliar — and it'd be inconsistent with the DirectXMath types this code
interoperates with directly.

### 2. Macros are allowed for profiler markers and assertions

Google's guide is wary of macros in general. This project allows them for
exactly two purposes: Tracy profiler zone markers, and assertions.

**Why:** Tracy's zone macros need to capture `__FILE__`/`__LINE__` at the
call site and must compile away to nothing when profiling is disabled — no
function can do either. Assertions need the same disappearing-in-Release
property. Both are the standard idiom for their job; there's no cleaner
non-macro equivalent.

### 3. Source files use `.cpp`, not `.cc`

Google's guide specifies `.cc`/`.h`. This project uses `.cpp`/`.h`.

**Why:** `.cpp` is the dominant convention in game and graphics code, and
matches Visual Studio's own project templates and every DirectX sample
you'll be reading side by side with this repo.

### 4. No exceptions

Consistent with Google's own default for most existing C++ codebases, stated
explicitly here: this project does not use C++ exceptions.

**Why:** Exception unwind cost is unpredictable, which is a bad fit for a
render loop or particle simulation running every frame. D3D12's own error
model is `HRESULT`-based, not exception-based, and the approved third-party
dependencies (Tracy, Dear ImGui, D3D12MemoryAllocator) are all written
assuming exceptions are off.

### 5. No RTTI

Also consistent with Google's own default, stated explicitly: this project
does not use `dynamic_cast` or `typeid`.

**Why:** Same predictability argument as exceptions — RTTI cost doesn't
belong in hot paths. For the one place you'd actually want runtime type
information (identifying a COM interface), D3D12's own `QueryInterface`
already provides a type-safe mechanism, making language-level RTTI
redundant.

### 6. `#pragma once` instead of include guards

Google's guide mandates manual `#ifndef` / `#define` / `#endif` include
guards, for portability across many compilers.

**Why:** This is a single-platform MSVC/clang-cl project — the portability
concern Google is guarding against doesn't apply here. `#pragma once` is
supported by every compiler this project will ever be built with, is less
boilerplate, and eliminates an entire class of copy-paste guard-macro bugs
(wrong macro name, collision between two files that reused one).
