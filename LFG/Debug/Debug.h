#pragma once
#include <Windows.h>

enum class LogLevel
{
	Info,
	Warning,
	Error,
	D3D
};

namespace Debug
{
	inline bool IsDebugMode = false;

	bool IsConsoleAttached();
	HANDLE GetConsoleHandle();
	void SetDebugMode(bool enable);
	void SetConsoleColor(WORD color);
	bool Check(const char* Name, bool result);
	bool Check(const char* name, uintptr_t pointer);
	void Print(LogLevel logLevel, const char* format, ...);
	void Info(const char* format, ...);
	void Error(const char* format, ...);
	void Warn(const char* format, ...);
}