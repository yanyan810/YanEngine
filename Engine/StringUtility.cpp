#include "StringUtility.h"

namespace StringUtility {

	// std::wstring -> std::string の変換
	std::string ConvertString(const std::wstring& str) {
		if (str.empty()) {
			return {};
		}

		int size_needed = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string result(size_needed - 1, 0); // -1 to exclude null terminator
		WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &result[0], size_needed, nullptr, nullptr);
		return result;
	}

	// std::string -> std::wstring の変換
	std::wstring ConvertString(const std::string& str) {
		if (str.empty()) {
			return {};
		}

		int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
		std::wstring result(size_needed - 1, 0); // -1 to exclude null terminator
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size_needed);
		return result;
	}

}