// Agility SDK opt-in. This is an explicitly authorized exception to the
// CLAUDE.md hard rule against writing RHI code -- see CLAUDE.md and
// docs/adr/0001-tech-stack.md for why.
//
// Windows only ships a D3D12 core DLL tied to the installed OS build. The
// Agility SDK lets an app redistribute and opt into a specific, newer D3D12
// runtime instead of being at the mercy of whatever version happens to be
// on the end user's machine.
//
// The loader only honors this if the executable exports two symbols with
// these exact names, and the redistributable DLLs (D3D12Core.dll,
// d3d12SDKLayers.dll) are present in the subfolder named by
// D3D12SDKPath, next to the exe. Both conditions have to hold -- see the
// post-build copy step in CMakeLists.txt for the DLL deployment half of
// this. If either is missing, D3D12CreateDevice() silently falls back to
// the OS-inbox core: no error, just quietly missing whatever the newer
// Agility SDK version provides (bug fixes, and for this project's purposes,
// some debug-layer / GPU-Based Validation behavior the smoke test checks
// for).
//
// The numeric SDK version below must match the fetched package: for
// Microsoft.Direct3D.D3D12 version "1.619.5", the exported version is the
// middle segment, 619 (see cmake/FetchAgilitySDK.cmake).
#include <d3d12.h>  // pulls in windows.h, for UINT

extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = 619;
__declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\";
}
