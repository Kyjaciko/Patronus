#pragma once

#include "DXSampleHelper.h"
#include "Win32Application.h"
#include "../platform/input/Mouse.h"
#include "../platform/input/Keyboard.h"

class DXSample
{
public:
  DXSample(UINT width, UINT height, std::wstring name);
  virtual ~DXSample();

  virtual void OnInit() = 0;
  virtual void OnUpdate() = 0;
  virtual void OnRender() = 0;
  virtual void OnSizeChanged(UINT width, UINT height, bool minimized) = 0;
  virtual void OnDestroy() = 0;

  // Accessors.
  UINT GetWidth() const           { return m_width; }
  UINT GetHeight() const          { return m_height; }
  const WCHAR* GetTitle() const   { return m_title.c_str(); }
  Mouse* GetMouse() const         { return m_mouse.get(); }
  Keyboard* GetKeyboard() const   { return m_keyboard.get(); }

  void ParseCommandLineArgs(_In_reads_(argc) WCHAR* argv[], int argc);

protected:
  std::wstring GetAssetFullPath(LPCWSTR assetName);

  void GetHardwareAdapter(
    _In_ IDXGIFactory1* pFactory,
    _Outptr_result_maybenull_ IDXGIAdapter1** ppAdapter,
    bool requestHighPerformanceAdapter = false);

  void SetCustomWindowText(LPCWSTR text);

  // Viewport dimensions.
  UINT m_width;
  UINT m_height;
  float m_aspectRatio;

  // Whether or not tearing is available for fullscreen borderless windowed mode.
  bool m_VSync = false;
  BOOL m_tearingSupport = FALSE;

  // Adapter info.
  bool m_useWarpDevice;

  std::unique_ptr<Mouse> m_mouse;
  std::unique_ptr<Keyboard> m_keyboard;

private:
  // Root assets path.
  std::wstring m_assetsPath;

  // Window title.
  std::wstring m_title;
};