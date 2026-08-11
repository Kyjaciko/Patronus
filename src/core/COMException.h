#pragma once

#include <string>
#include <comdef.h>
#include <windows.h>
#include <cassert>

#include "StringHelper.h"

namespace windows
{

class COMException
{
public:
  COMException(HRESULT hr, const std::wstring& msg, const std::string& file, const std::string& function, int line)
  {
    _com_error error(hr);
    m_message = L"Message: " + msg + L"\n";
    m_message += error.ErrorMessage();
    m_message += L"\nFile: " + StringHelper::StringToWide(file) + L"\n";
    m_message += L"Function: " + StringHelper::StringToWide(function) + L"\n";
    m_message += L"Line: " + std::to_wstring(line) + L"\n";
  }

  COMException(HRESULT hr, const std::string& msg, const std::string& file, const std::string& function, int line)
    : COMException(hr, StringHelper::StringToWide(msg), file, function, line)
  {
  }

  const wchar_t* what() const
  {
    return m_message.c_str();
  }

private:
  std::wstring m_message;
};

}

#define COM_ERROR_IF_FAILED(hr, msg) do { \
  if (FAILED(hr)) \
  { \
    windows::COMException exception(hr, msg, __FILE__, __FUNCTION__, __LINE__); \
    std::wstring error_message = exception.what(); \
    MessageBoxW(nullptr, error_message.c_str(), L"Error", MB_ICONERROR); \
    exit(-1); \
  } \
} while(0) // 'do { ... } while(0)' is needed to avoid 'dangling else'.