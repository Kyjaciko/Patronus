#pragma once

#include <string>
#include <algorithm>

class StringHelper
{
public:
	static std::wstring StringToWide(const std::string& str)
	{
		std::wstring wide_string(str.begin(), str.end());
		return wide_string;
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