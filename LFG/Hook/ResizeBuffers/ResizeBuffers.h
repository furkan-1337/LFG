#pragma once
#include <Debug/Debug.h>
#include <DirectX/DirectX.h>
#include <Hook/Common.h>

namespace ResizeBuffers
{
	typedef HRESULT(__stdcall* ResizeBuffers_t)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
	inline ResizeBuffers_t Original = nullptr;

	bool Hook();
	void Unhook();
}