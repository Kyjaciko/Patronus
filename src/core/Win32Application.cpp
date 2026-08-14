#include "stdafx.h"
#include "Win32Application.h"

HWND Win32Application::m_hwnd = nullptr;
bool Win32Application::m_fullscreenMode = false;
RECT Win32Application::m_windowRect;
using Microsoft::WRL::ComPtr;

int Win32Application::Run(DXSample* pSample, HINSTANCE hInstance, int nCmdShow)
{
  // Parse the command line parameters
  int argc;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  pSample->ParseCommandLineArgs(argv, argc);
  LocalFree(argv);

  // Initialize the window class.
  WNDCLASSEX windowClass = { 0 };
  windowClass.cbSize = sizeof(WNDCLASSEX);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = WindowProc;
  windowClass.hInstance = hInstance;
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.lpszClassName = L"DXSampleClass";
  RegisterClassEx(&windowClass);

  RECT windowRect = { 0, 0, static_cast<LONG>(pSample->GetWidth()), static_cast<LONG>(pSample->GetHeight()) };
  AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

  // Create the window and store a handle to it.
  m_hwnd = CreateWindow(
    windowClass.lpszClassName,
    pSample->GetTitle(),
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    windowRect.right - windowRect.left,
    windowRect.bottom - windowRect.top,
    nullptr,        // We have no parent window.
    nullptr,        // We aren't using menus.
    hInstance,
    pSample);

  // Initialize the sample. OnInit is defined in each child-implementation of DXSample.
  pSample->OnInit();

  ShowWindow(m_hwnd, nCmdShow);

  // Main sample loop.
  MSG msg = {};
  while (msg.message != WM_QUIT)
  {
    // Process any messages in the queue.
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  pSample->OnDestroy();

  // Return this part of the WM_QUIT message to Windows.
  return static_cast<char>(msg.wParam);
}

void Win32Application::ToggleFullscreenWindow(IDXGISwapChain* pSwapChain)
{
  assert(pSwapChain != nullptr);

  if (m_fullscreenMode)
  {
    // Restore the window's attributes and size.
    SetWindowLong(m_hwnd, GWL_STYLE, m_windowStyle);
    
    SetWindowPos(
      m_hwnd,
      HWND_NOTOPMOST,
      m_windowRect.left,
      m_windowRect.top,
      m_windowRect.right - m_windowRect.left,
      m_windowRect.bottom - m_windowRect.top,
      SWP_FRAMECHANGED | SWP_NOACTIVATE
    );

    ShowWindow(m_hwnd, SW_NORMAL);
  }
  else
  {
    // Save the old window rect so we can restore it when exiting fullscreen mode.
    GetWindowRect(m_hwnd, &m_windowRect);

    // Make the window borderless so that the client area can fill the screen.
    SetWindowLong(m_hwnd, GWL_STYLE, m_windowStyle & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME));

    RECT fullscreenWindowRect;
    if (pSwapChain)
    {
      // Get the settings of the display on which the app's window is currently displayed
      ComPtr<IDXGIOutput> pOutput;
      COM_ERROR_IF_FAILED(pSwapChain->GetContainingOutput(&pOutput), "Failed to get the output that contains the majority of the client area of the target window.");

      DXGI_OUTPUT_DESC Desc;
      COM_ERROR_IF_FAILED(pOutput->GetDesc(&Desc), "Failed to get a description of the output.");
      fullscreenWindowRect = Desc.DesktopCoordinates;
    }

    SetWindowPos(
      m_hwnd,
      HWND_TOPMOST,
      fullscreenWindowRect.left,
      fullscreenWindowRect.top,
      fullscreenWindowRect.right - fullscreenWindowRect.left,
      fullscreenWindowRect.bottom - fullscreenWindowRect.top,
      SWP_FRAMECHANGED | SWP_NOACTIVATE
    );

    ShowWindow(m_hwnd, SW_MAXIMIZE);
  }

  m_fullscreenMode = !m_fullscreenMode;
}

// Main message handler for the sample.
LRESULT CALLBACK Win32Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  DXSample* pSample = reinterpret_cast<DXSample*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

  switch (message)
  {
  case WM_CREATE:
    {
      // Save the DXSample* passed in to CreateWindow.
      LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
      SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
    }
    return 0;

  case WM_KEYDOWN:
    if (pSample)
    {
      pSample->OnKeyDown(static_cast<UINT8>(wParam));
    }
    return 0;

  case WM_KEYUP:
    if (pSample)
    {
      pSample->OnKeyUp(static_cast<UINT8>(wParam));
    }
    return 0;

  case WM_PAINT:
    if (pSample)
    {
      pSample->OnUpdate();
      pSample->OnRender();
    }
    return 0;

  case WM_SIZE:
  {
    if (!pSample)
      return 0;

    RECT client_rect{};
    GetClientRect(hWnd, &client_rect);
    pSample->OnSizeChanged(client_rect.right - client_rect.left, client_rect.bottom - client_rect.top, wParam == SIZE_MINIMIZED);

    return 0;
  }

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  // Handle any messages the switch statement didn't.
  return DefWindowProc(hWnd, message, wParam, lParam);
}