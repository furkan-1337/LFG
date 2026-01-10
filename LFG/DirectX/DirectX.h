#pragma once
#include <Windows.h>
#include <cstdint>

#define USE_D3D9   0
#define USE_D3D10  0
#define USE_D3D11  1
#define USE_D3D12  0

#if defined(_M_X64)
typedef uint64_t uint150_t;
#else
typedef uint32_t uint150_t;
#endif

#if USE_D3D9
# include <d3d9.h>
#endif

#if USE_D3D10
# include <dxgi.h>
# include <d3d10_1.h>
# include <d3d10.h>
#endif

#if USE_D3D11
# include <dxgi.h>
# include <d3d11.h>
#endif

#if USE_D3D12
# include <dxgi.h>
# include <d3d12.h>
#endif

enum class D3DVersion
{
	Direct3D9,
	Direct3D10,
	Direct3D11,
	Direct3D12,
};

namespace DirectX
{
	inline const char* WindowName = "DirectX Window";
	inline const char* ClassName = "DirectXWindow";
	HWND CreateDummyWindow();
	void DestroyDummyWindow(HWND hWnd);
	uint150_t* GetMethodTable(D3DVersion backend);
}
