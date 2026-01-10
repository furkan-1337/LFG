#include "DirectX.h"

HWND DirectX::CreateDummyWindow()
{
	WNDCLASSEX windowClass;
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = DefWindowProc;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = GetModuleHandle(NULL);
	windowClass.hIcon = NULL;
	windowClass.hCursor = NULL;
	windowClass.hbrBackground = NULL;
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = DirectX::ClassName;
	windowClass.hIconSm = NULL;

	::RegisterClassEx(&windowClass);

	return ::CreateWindow(windowClass.lpszClassName, DirectX::WindowName, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, windowClass.hInstance, NULL);

}

void DirectX::DestroyDummyWindow(HWND hWnd)
{
	::DestroyWindow(hWnd);
	::UnregisterClass(DirectX::ClassName, GetModuleHandle(NULL));
}

uint150_t* DirectX::GetMethodTable(D3DVersion version)
{
	uint150_t* Methods = nullptr;
	auto window = DirectX::CreateDummyWindow();

	if (version == D3DVersion::Direct3D9)
	{
#if USE_D3D9
		HMODULE libD3D9;
		if ((libD3D9 = ::GetModuleHandle("d3d9.dll")) == NULL)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		void* Direct3DCreate9;
		if ((Direct3DCreate9 = ::GetProcAddress(libD3D9, "Direct3DCreate9")) == NULL)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		LPDIRECT3D9 direct3D9;
		if ((direct3D9 = ((LPDIRECT3D9(__stdcall*)(uint32_t))(Direct3DCreate9))(D3D_SDK_VERSION)) == NULL)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		D3DDISPLAYMODE displayMode;
		if (direct3D9->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode) < 0)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		D3DPRESENT_PARAMETERS params;
		params.BackBufferWidth = 0;
		params.BackBufferHeight = 0;
		params.BackBufferFormat = displayMode.Format;
		params.BackBufferCount = 0;
		params.MultiSampleType = D3DMULTISAMPLE_NONE;
		params.MultiSampleQuality = NULL;
		params.SwapEffect = D3DSWAPEFFECT_DISCARD;
		params.hDeviceWindow = window;
		params.Windowed = 1;
		params.EnableAutoDepthStencil = 0;
		params.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
		params.Flags = NULL;
		params.FullScreen_RefreshRateInHz = 0;
		params.PresentationInterval = 0;

		LPDIRECT3DDEVICE9 device;
		if (direct3D9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &params, &device) < 0)
		{
			direct3D9->Release();
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		Methods = (uint150_t*)::calloc(119, sizeof(uint150_t));
		::memcpy(Methods, *(uint150_t**)device, 119 * sizeof(uint150_t));

		direct3D9->Release();
		direct3D9 = NULL;

		device->Release();
		device = NULL;

		DirectX::DestroyDummyWindow(window);
		return Methods;
#endif
	}
	else if (version == D3DVersion::Direct3D10)
	{
#if USE_D3D10
		HMODULE libDXGI;
		HMODULE libD3D10;
		if ((libDXGI = ::GetModuleHandle("dxgi.dll")) == NULL || (libD3D10 = ::GetModuleHandle("d3d10.dll")) == NULL)
		{
			::DestroyWindow(window);
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		void* CreateDXGIFactory;
		if ((CreateDXGIFactory = ::GetProcAddress(libDXGI, "CreateDXGIFactory")) == NULL)
		{
			::DestroyWindow(window);
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		IDXGIFactory* factory;
		if (((long(__stdcall*)(const IID&, void**))(CreateDXGIFactory))(__uuidof(IDXGIFactory), (void**)&factory) < 0)
		{
			::DestroyWindow(window);
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		IDXGIAdapter* adapter;
		if (factory->EnumAdapters(0, &adapter) == DXGI_ERROR_NOT_FOUND)
		{
			::DestroyWindow(window);
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		void* D3D10CreateDeviceAndSwapChain;
		if ((D3D10CreateDeviceAndSwapChain = ::GetProcAddress(libD3D10, "D3D10CreateDeviceAndSwapChain")) == NULL)
		{
			::DestroyWindow(window);
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		DXGI_RATIONAL refreshRate;
		refreshRate.Numerator = 60;
		refreshRate.Denominator = 1;

		DXGI_MODE_DESC bufferDesc;
		bufferDesc.Width = 100;
		bufferDesc.Height = 100;
		bufferDesc.RefreshRate = refreshRate;
		bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

		DXGI_SAMPLE_DESC sampleDesc;
		sampleDesc.Count = 1;
		sampleDesc.Quality = 0;

		DXGI_SWAP_CHAIN_DESC swapChainDesc;
		swapChainDesc.BufferDesc = bufferDesc;
		swapChainDesc.SampleDesc = sampleDesc;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 1;
		swapChainDesc.OutputWindow = window;
		swapChainDesc.Windowed = 1;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		IDXGISwapChain* swapChain;
		ID3D10Device* device;

		if (((long(__stdcall*)(
			IDXGIAdapter*,
			D3D10_DRIVER_TYPE,
			HMODULE,
			UINT,
			UINT,
			DXGI_SWAP_CHAIN_DESC*,
			IDXGISwapChain**,
			ID3D10Device**))(D3D10CreateDeviceAndSwapChain))(adapter, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_SDK_VERSION, &swapChainDesc, &swapChain, &device) < 0)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		Methods = (uint150_t*)::calloc(116, sizeof(uint150_t));
		::memcpy(Methods, *(uint150_t**)swapChain, 18 * sizeof(uint150_t));
		::memcpy(Methods + 18, *(uint150_t**)device, 98 * sizeof(uint150_t));

		swapChain->Release();
		swapChain = NULL;

		device->Release();
		device = NULL;

		DirectX::DestroyDummyWindow(window);
		return Methods;
#endif
	}
	else if (version == D3DVersion::Direct3D11)
	{
#if USE_D3D11
		HMODULE libD3D11;
		if ((libD3D11 = ::GetModuleHandle("d3d11.dll")) == NULL)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		void* D3D11CreateDeviceAndSwapChain;
		if ((D3D11CreateDeviceAndSwapChain = ::GetProcAddress(libD3D11, "D3D11CreateDeviceAndSwapChain")) == NULL)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		D3D_FEATURE_LEVEL featureLevel;
		const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_11_0 };

		DXGI_RATIONAL refreshRate;
		refreshRate.Numerator = 60;
		refreshRate.Denominator = 1;

		DXGI_MODE_DESC bufferDesc;
		bufferDesc.Width = 100;
		bufferDesc.Height = 100;
		bufferDesc.RefreshRate = refreshRate;
		bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

		DXGI_SAMPLE_DESC sampleDesc;
		sampleDesc.Count = 1;
		sampleDesc.Quality = 0;

		DXGI_SWAP_CHAIN_DESC swapChainDesc;
		swapChainDesc.BufferDesc = bufferDesc;
		swapChainDesc.SampleDesc = sampleDesc;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 1;
		swapChainDesc.OutputWindow = window;
		swapChainDesc.Windowed = 1;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		IDXGISwapChain* swapChain;
		ID3D11Device* device;
		ID3D11DeviceContext* context;

		if (((long(__stdcall*)(
			IDXGIAdapter*,
			D3D_DRIVER_TYPE,
			HMODULE,
			UINT,
			const D3D_FEATURE_LEVEL*,
			UINT,
			UINT,
			const DXGI_SWAP_CHAIN_DESC*,
			IDXGISwapChain**,
			ID3D11Device**,
			D3D_FEATURE_LEVEL*,
			ID3D11DeviceContext**))(D3D11CreateDeviceAndSwapChain))(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, featureLevels, 1, D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device, &featureLevel, &context) < 0)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		Methods = (uint150_t*)::calloc(205, sizeof(uint150_t));
		::memcpy(Methods, *(uint150_t**)swapChain, 18 * sizeof(uint150_t));
		::memcpy(Methods + 18, *(uint150_t**)device, 43 * sizeof(uint150_t));
		::memcpy(Methods + 18 + 43, *(uint150_t**)context, 144 * sizeof(uint150_t));

		swapChain->Release();
		swapChain = NULL;

		device->Release();
		device = NULL;

		context->Release();
		context = NULL;

		DirectX::DestroyDummyWindow(window);
		return Methods;
#endif
	}
	else if (version == D3DVersion::Direct3D12)
	{
#if USE_D3D12
		HMODULE libDXGI;
		HMODULE libD3D12;
		if ((libDXGI = ::GetModuleHandle(("dxgi.dll"))) == NULL || (libD3D12 = ::GetModuleHandle("d3d12.dll")) == NULL)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		void* CreateDXGIFactory;
		if ((CreateDXGIFactory = ::GetProcAddress(libDXGI, "CreateDXGIFactory")) == NULL)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		IDXGIFactory* factory;
		if (((long(__stdcall*)(const IID&, void**))(CreateDXGIFactory))(__uuidof(IDXGIFactory), (void**)&factory) < 0)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		IDXGIAdapter* adapter;
		if (factory->EnumAdapters(0, &adapter) == DXGI_ERROR_NOT_FOUND)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		void* D3D12CreateDevice;
		if ((D3D12CreateDevice = ::GetProcAddress(libD3D12, "D3D12CreateDevice")) == NULL)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		ID3D12Device* device;
		if (((long(__stdcall*)(IUnknown*, D3D_FEATURE_LEVEL, const IID&, void**))(D3D12CreateDevice))(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&device) < 0)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Priority = 0;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 0;

		ID3D12CommandQueue* commandQueue;
		if (device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void**)&commandQueue) < 0)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		ID3D12CommandAllocator* commandAllocator;
		if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&commandAllocator) < 0)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		ID3D12GraphicsCommandList* commandList;
		if (device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, NULL, __uuidof(ID3D12GraphicsCommandList), (void**)&commandList) < 0)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		DXGI_RATIONAL refreshRate;
		refreshRate.Numerator = 60;
		refreshRate.Denominator = 1;

		DXGI_MODE_DESC bufferDesc;
		bufferDesc.Width = 100;
		bufferDesc.Height = 100;
		bufferDesc.RefreshRate = refreshRate;
		bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

		DXGI_SAMPLE_DESC sampleDesc;
		sampleDesc.Count = 1;
		sampleDesc.Quality = 0;

		DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
		swapChainDesc.BufferDesc = bufferDesc;
		swapChainDesc.SampleDesc = sampleDesc;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.OutputWindow = window;
		swapChainDesc.Windowed = 1;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		IDXGISwapChain* swapChain;
		if (factory->CreateSwapChain(commandQueue, &swapChainDesc, &swapChain) < 0)
		{
			DirectX::DestroyDummyWindow(window);
			return nullptr;
		}

		Methods = (uint150_t*)::calloc(150, sizeof(uint150_t));
		::memcpy(Methods, *(uint150_t**)device, 44 * sizeof(uint150_t));
		::memcpy(Methods + 44, *(uint150_t**)commandQueue, 19 * sizeof(uint150_t));
		::memcpy(Methods + 44 + 19, *(uint150_t**)commandAllocator, 9 * sizeof(uint150_t));
		::memcpy(Methods + 44 + 19 + 9, *(uint150_t**)commandList, 60 * sizeof(uint150_t));
		::memcpy(Methods + 44 + 19 + 9 + 60, *(uint150_t**)swapChain, 18 * sizeof(uint150_t));

		device->Release();
		device = NULL;

		commandQueue->Release();
		commandQueue = NULL;

		commandAllocator->Release();
		commandAllocator = NULL;

		commandList->Release();
		commandList = NULL;

		swapChain->Release();
		swapChain = NULL;

		DirectX::DestroyDummyWindow(window);
		return Methods;
#endif
	}
	return nullptr;
}
