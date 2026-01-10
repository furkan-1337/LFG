#include <Windows.h>
#include <process.h>
#include <Debug/Debug.h>
#include <Hook/Engine/HookEngine.h>
#include <Hook/Present/Present.h>
#include <Hook/ResizeBuffers/ResizeBuffers.h>
HMODULE g_hModule = nullptr;

DWORD WINAPI MainThread(LPVOID lpParam)
{
    Debug::SetDebugMode(true);

    Debug::Info("========================================");
    Debug::Info(" Lufzy's Frame Generation initialized ");
    Debug::Info("========================================");

    if (Debug::Check("Hook Engine initialization", HookEngine::Initialize()))
    {
        if (
            Debug::Check("DX Present hook", Present::Hook()) &&
            Debug::Check("ResizeBuffers hook", ResizeBuffers::Hook())
            )
        {
            Debug::Info("All hooks installed successfully.");
            Debug::Info("LFG injection completed without errors.");
        }
        else
        {
            Debug::Error("One or more hooks failed to install.");
        }
    }
    else
    {
        Debug::Error("Hook Engine failed to initialize.");
    }

    return 0;
}

void Unload()
{
    FreeLibraryAndExitThread(g_hModule, 0);
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);

        HANDLE hThread = CreateThread(
            nullptr,
            0,
            MainThread,
            hModule,
            0,
            nullptr
        );

        if (hThread)
            CloseHandle(hThread);
        break;
    }
    return TRUE;
}