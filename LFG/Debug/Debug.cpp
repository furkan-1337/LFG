#include "Debug.h"
#include <cstdio>

bool Debug::IsConsoleAttached()
{
    return GetConsoleWindow() != NULL;
}

HANDLE Debug::GetConsoleHandle()
{
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

void Debug::SetDebugMode(bool enable)
{
    if (enable)
    {
        AllocConsole();

        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONIN$", "r", stdin);
        freopen_s(&fp, "CONOUT$", "w", stderr);

        SetConsoleTitleA("Lufzy's Frame Gen - Debug");
        IsDebugMode = true;
    }
    else
    {
        if (IsConsoleAttached())
            FreeConsole();
        IsDebugMode = false;
    }
}

void Debug::SetConsoleColor(WORD color)
{
    static HANDLE hConsole = GetConsoleHandle();
    SetConsoleTextAttribute(hConsole, color);
}

bool Debug::Check(const char* name, bool result)
{
    Debug::Print(result ? LogLevel::Info : LogLevel::Error, result ? "%s initialized successfully!" : "%s failed to initialize!", name);
    return result;
}

bool Debug::Check(const char* name, uintptr_t pointer)
{
    auto result = pointer != NULL;
    Debug::Print(result ? LogLevel::Info : LogLevel::Error, result ? "%s: 0x%p" : "%s was null!", name, pointer);
    return result;
}

void Debug::Print(LogLevel logLevel, const char* format, ...)
{
    if (!(IsDebugMode && IsConsoleAttached()))
        return;

    if (!format)
        return;

    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    const char* prefix = "";
    WORD color = 7; // Default: Gray

    switch (logLevel)
    {
    case LogLevel::Info:
        prefix = "[INFO] ";
        color = 11;  // Gray
        break;
    case LogLevel::D3D:
        prefix = "[D3D] ";
        color = 13; // Cyan
        break;
    case LogLevel::Warning:
        prefix = "[WARN] ";
        color = 14; // Yellow
        break;
    case LogLevel::Error:
        prefix = "[ERROR] ";
        color = 12; // Red
        break;
    }

    // Prefix renkli
    SetConsoleColor(color);
    printf("%s", prefix);

    // Renk reset → mesaj normal beyaz
    SetConsoleColor(7);
    printf("%s\n", buffer);
}

void Debug::Info(const char* format, ...)
{
    if (!(IsDebugMode && IsConsoleAttached())) return;
    
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    Print(LogLevel::Info, "%s", buffer);
}

void Debug::Error(const char* format, ...)
{
    if (!(IsDebugMode && IsConsoleAttached())) return;

    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    Print(LogLevel::Error, "%s", buffer);
}

void Debug::Warn(const char* format, ...)
{
    if (!(IsDebugMode && IsConsoleAttached())) return;

    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    Print(LogLevel::Warning, "%s", buffer);
}
