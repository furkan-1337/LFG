#pragma once
#include <Debug/Debug.h>
#include <DirectX/DirectX.h>
#include <Hook/Common.h>

namespace Present
{
	typedef HRESULT(__stdcall* Present) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
	inline Present Original = nullptr;

	inline ID3D11Device* Device = nullptr;
	inline ID3D11DeviceContext* Context = nullptr;
	inline ID3D11RenderTargetView* RenderTargetView = nullptr;

	inline WNDPROC OriginalWndProc = NULL;

	bool Hook();
	void Unhook();
}