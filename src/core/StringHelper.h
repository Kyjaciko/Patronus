#pragma once

#include <string>
#include <algorithm>
#include <windows.h>

class StringHelper
{
public:
	static std::wstring StringToWide(const std::string& str)
	{
		std::wstring wide_string(str.begin(), str.end());
		return wide_string;
	}

	static std::string WideToString(const std::wstring& w_str)
	{
		if (w_str.empty())
			return std::string{};

		const int len = static_cast<int>(w_str.size());
		const int size = WideCharToMultiByte(CP_UTF8, 0, w_str.data(), len, nullptr, 0, nullptr, nullptr);
		std::string output(size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w_str.data(), len, &output[0], size, nullptr, nullptr);
		return output;
	}

	static std::string GetDirectoryFromPath(const std::string& filePath)
	{
		size_t off_1 = filePath.find_last_of('\\');
		size_t off_2 = filePath.find_last_of('/');

		// No slash or backslash?
		if (off_1 == std::string::npos && off_2 == std::string::npos) return "";

		else if (off_1 == std::string::npos) return filePath.substr(0, off_2);

		else if (off_2 == std::string::npos) return filePath.substr(0, off_1);

		// Both exist? -> Use greatest offset.
		return filePath.substr(0, std::max(off_1, off_2));
	}

	static std::string GetFileExtension(const std::string& fileName)
	{
		size_t off = fileName.find_last_of('.');
		if (off == std::string::npos) return {};

		return std::string(fileName.substr(off + 1));
	}
};