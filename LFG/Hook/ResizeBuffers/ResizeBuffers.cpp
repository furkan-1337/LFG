#include "ResizeBuffers.h"
#include <Dependencies/ImGui/backends/imgui_impl_dx11.h>
#include <Hook/Present/Present.h>

extern HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

bool ResizeBuffers::Hook()
{
	auto address = Common::GetMethodByIndex(D3DVersion::Direct3D11, 13);
	if (address == nullptr)
		return false;
	bool isHooked = HookEngine::Hook((void*)address, hkResizeBuffers, (void**)&Original);
	Debug::Print(LogLevel::D3D, isHooked ? "ResizeBuffers (0x%p) succesfully hooked!" : "ResizeBuffers (0x%p) hook failed!", address);
	return isHooked;
}

void ResizeBuffers::Unhook()
{
	HookEngine::Unhook((void*)Original);
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    // 1. Mandatory Cleanup
    // We must release the RenderTargetView before calling the original ResizeBuffers.
    // If the ref count is not zero, the original call will return DXGI_ERROR_INVALID_CALL.
    if (Present::RenderTargetView)
    {
        Present::Context->OMSetRenderTargets(0, nullptr, nullptr);
        Present::RenderTargetView->Release();
        Present::RenderTargetView = nullptr;
    }

    // 2. Notify Middleware
    // ImGui_ImplDX11 needs to drop its internal references to the render target as well.
    ImGui_ImplDX11_InvalidateDeviceObjects();

    // 3. Execute Original
    // Force Triple Buffering to prevent starvation during Double Present
    if (BufferCount < 3) BufferCount = 3;
    
    HRESULT hr = ResizeBuffers::Original(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    // 4. Re-acquire Resources
    if (SUCCEEDED(hr))
    {
        ID3D11Texture2D* pBackBuffer = nullptr;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)))
        {
            Present::Device->CreateRenderTargetView(pBackBuffer, NULL, &Present::RenderTargetView);
            pBackBuffer->Release();
        }

        // 5. Restore Middleware
        ImGui_ImplDX11_CreateDeviceObjects();
    }

    return hr;
}