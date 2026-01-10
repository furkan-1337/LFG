#include "HookEngine.h"

bool HookEngine::Initialize()
{
	if (IsInitialized)
		return true;

	if (MH_Initialize() != MH_OK)
		return false;

	IsInitialized = true;
	return true;
}

bool HookEngine::Uninitialize()
{
	if (!IsInitialized)
		return true;

	MH_DisableHook(MH_ALL_HOOKS);
	if (MH_Uninitialize() != MH_OK)
		return false;

	IsInitialized = false;
	return true;
}

bool HookEngine::Hook(void* targetAddress, void* function, void** original)
{
	if (!IsInitialized && !Initialize())
		return false;

	if (MH_CreateHook(targetAddress, function, original) != MH_OK ||
		MH_EnableHook(targetAddress) != MH_OK)
		return false;

	return true;
}

bool HookEngine::Unhook(void* targetAddress)
{
	if (MH_DisableHook(reinterpret_cast<LPVOID>(targetAddress)) != MH_OK)
		return false;

	if (MH_RemoveHook(reinterpret_cast<LPVOID>(targetAddress)) != MH_OK)
		return false;

	return true;
}