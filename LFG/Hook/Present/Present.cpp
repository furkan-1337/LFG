#include "Present.h"
#include "Pipeline/Generation/FrameGeneration.h"
#include "UI/Menu.h"
#include "UI/DebugOverlay.h"
#include <Dependencies/ImGui/imgui.h>
#include <Dependencies/ImGui/backends/imgui_impl_win32.h>
#include <Dependencies/ImGui/backends/imgui_impl_dx11.h>
#include <chrono>

extern HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

bool Present::Hook()
{
	auto address = Common::GetMethodByIndex(D3DVersion::Direct3D11, 8);
	if (address == nullptr)
		return false;

	bool isHooked = HookEngine::Hook((void*)address, hkPresent, (void**)&Original);
	Debug::Print(LogLevel::D3D, isHooked ? "Present (0x%p) succesfully hooked!" : "Present (0x%p) hook failed!", address);
	return isHooked;
}

void Present::Unhook()
{
	HookEngine::Unhook((void*)Original);
}

// [PACER HELPER]
namespace
{
	class FramePacer
	{
	public:
		void Wait(int targetFPS)
		{
			if (targetFPS <= 0) return;

			auto currentTime = std::chrono::high_resolution_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastPresentTime).count();
			long targetFrameTime = 1000000 / targetFPS;

			if (elapsed < targetFrameTime)
			{
				long waitTime = targetFrameTime - elapsed;
				if (waitTime > 2000) Sleep((DWORD)((waitTime - 1000) / 1000));
				while (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - lastPresentTime).count() < targetFrameTime) {
					// Spin lock for precision
				}
			}
			lastPresentTime = std::chrono::high_resolution_clock::now();
		}

	private:
		std::chrono::high_resolution_clock::time_point lastPresentTime = std::chrono::high_resolution_clock::now();
	};

	FramePacer g_Pacer;
}

static bool IsImGuiInitialized = false;
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	// [DYNAMIC RATIO TIME TRACKING]
	// We need TWO metrics:
	// 1. Game CPU Time (for Dynamic Ratio load calculation) -> Entry - LastReturn
	// 2. Full Frame Latency (for User Display / FPS) -> Entry - LastEntry
	
	static auto lastReturnTime = std::chrono::high_resolution_clock::now();
	static auto lastEntryTime = std::chrono::high_resolution_clock::now();
	
	auto entryTime = std::chrono::high_resolution_clock::now();
	
	// 1. Game CPU Time (pure internal frame time)
	long pureFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(entryTime - lastReturnTime).count();

	// 2. Full Loop Time (App Latency)
	long fullFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(entryTime - lastEntryTime).count();
	lastEntryTime = entryTime; // Update for next frame
	
	// [LATENCY TRACKING]
	// NVIDIA Reflex metric usually assumes roughly "1 Frame + Render Queue".
	// Using fullFrameTime (Interval) is a safe "App Latency" lower bound.
	if (fullFrameTime > 0)
	{
		float latMs = (float)fullFrameTime / 1000.0f;
		UI::DebugOverlay::SetInputLatency(latMs);
	}

	if (!IsImGuiInitialized)
	{
		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&Present::Device)))
		{
			Present::Device->GetImmediateContext(&Present::Context);
			DXGI_SWAP_CHAIN_DESC sd;
			pSwapChain->GetDesc(&sd);
			auto window = sd.OutputWindow;
			ID3D11Texture2D* pBackBuffer;
			pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
			Present::Device->CreateRenderTargetView(pBackBuffer, NULL, &Present::RenderTargetView);
			pBackBuffer->Release();
			Present::OriginalWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);

			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
			io.IniFilename = NULL;
			io.LogFilename = NULL;

			ImGui_ImplWin32_Init(window);
			ImGui_ImplDX11_Init(Present::Device, Present::Context);

			// Init Frame Generation Engine
			FrameGeneration::Instance().Initialize(Present::Device);

			IsImGuiInitialized = true;
		}
		else
			return Present::Original(pSwapChain, SyncInterval, Flags);
	}

	// [UI LOGIC - Prepare DrawData ONCE]
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	
	// Input Toggle
	static bool showMenu = true;
	if (GetAsyncKeyState(VK_INSERT) & 1) showMenu = !showMenu;
	
	UI::Menu::Render(showMenu);
	UI::DebugOverlay::Render();

	ImGui::Render();

	// [FRAME GENERATION LOGIC]
	bool isEnabled = FrameGeneration::Instance().IsEnabled();
	auto& settings = FrameGeneration::Instance().GetSettings();

	// [DYNAMIC RATIO CALCULATION]
	int targetForRatio = settings.DynamicTargetFPS > 0 ? settings.DynamicTargetFPS : settings.TargetFPS;
	if (isEnabled && settings.EnableDynamicRatio && targetForRatio > 0)
	{
		// Use Full Frame Time (Entry-to-Entry) to account for GPU bottlenecks / VSync.
		// pureFrameTime only measures CPU work, which can be tiny (1000+ FPS) even if game is GPU bound at 60.
		long inputTime = fullFrameTime; 
		
		// Avoid division by zero or invalid initial times
		if (inputTime < 1) inputTime = 1;

		// [STABILITY - EMA Smoothing]
		static double avgFrameTime = 0.0;
		if (avgFrameTime == 0.0) avgFrameTime = (double)inputTime; // Init

		// Alpha = 0.05 for very smooth transitions (approx 20 frames to settle)
		// Alpha = 0.05 for very smooth transitions (approx 20 frames to settle)
		const double alpha = 0.05; 
		avgFrameTime = avgFrameTime * (1.0 - alpha) + (double)inputTime * alpha;

		double realFPS = 1000000.0 / avgFrameTime;
		
		// Calculate how many total frames we need (Real + Gen) to hit Target
		// Ratio = Target / Real
		// e.g. Target 60, Real 60 -> Ratio 1.0 -> Gen 0 (1x)
		// e.g. Target 60, Real 30 -> Ratio 2.0 -> Gen 1 (2x)
		// e.g. Target 60, Real 20 -> Ratio 3.0 -> Gen 2 (3x)
		
		// e.g. Target 60, Real 20 -> Ratio 3.0 -> Gen 2 (3x)
		
		double ratio = (double)targetForRatio / realFPS;
		int neededGen = (int)round(ratio) - 1;
		
		// [STABILITY - Hysteresis]
		// Prevent flickering between e.g. 2x and 3x if ration fluctuates 2.49 <-> 2.51
		// We only switch if the new neededGen is consistently different or significantly different.
		// Simple approach: Use a "deadzone" around the transition points (x.5)
		// But since we use round(), the transition is at x.5.
		// Let's use the raw ratio to decide.
		
		int currentGen = settings.MultiFrameCount;
		if (neededGen != currentGen)
		{
			double threshold = 0.1; // Require 0.1 delta past the rounding point to switch
			// If we are increasing gen (e.g. 1 -> 2), ratio must be > 2.5 + 0.1 = 2.6
			// If we are decreasing gen (e.g. 2 -> 1), ratio must be < 2.5 - 0.1 = 2.4
			
			// Transition point to 'neededGen' from 'currentGen'
			double transitionPoint = (double)currentGen + 1.0 + (neededGen > currentGen ? 0.5 : -0.5); 
			
			// Check if we passed the threshold
			bool switchAllowed = false;
			if (neededGen > currentGen && ratio > (transitionPoint + threshold)) switchAllowed = true;
			else if (neededGen < currentGen && ratio < (transitionPoint - threshold)) switchAllowed = true;
			
			// Allow switch if jump is large (>1 step) just in case
			if (abs(neededGen - currentGen) > 1) switchAllowed = true;
			
			if (switchAllowed)
			{
				int maxGen = settings.EnableAggressiveDynamicMode ? 5 : 3;
			
				// Clamp to supported modes 
				if (neededGen < 0) neededGen = 0;
				if (neededGen > maxGen) neededGen = maxGen;
				
				settings.MultiFrameCount = neededGen;
			}
		}
		// Final Safety Clamp
		int maxGen = settings.EnableAggressiveDynamicMode ? 5 : 3;
		if (settings.MultiFrameCount < 0) settings.MultiFrameCount = 0;
		if (settings.MultiFrameCount > maxGen) settings.MultiFrameCount = maxGen;
	}
	
	int pacerFPS = settings.TargetFPS;
	if (settings.FPSCap && settings.CapMode == FrameGeneration::FrameGenSettings::FpsCapMode::Native)
	{
		// In Native mode, we want the REAL FPS to act as the limit.
		// If we are generating N frames, the total output FPS will be Real * (N+1).
		// To achieve this, each individual frame (Real or Generated) needs to be shorter.
		// Wait(Effective) where Effective = Target * (N+1).
		pacerFPS = settings.TargetFPS * (settings.MultiFrameCount + 1);
	}

	UINT presentFlags = Flags;
	UINT syncIntervalForReal = SyncInterval;

	if (isEnabled)
	{
		if (settings.DisableVSync)
		{
			syncIntervalForReal = 0;
			presentFlags |= 0x200; // DXGI_PRESENT_ALLOW_TEARING
		}

		// 2. Capture Current Frame
		FrameGeneration::Instance().Capture(pSwapChain);

		// 3. Multi-Frame Generation Loop
		int framesToGen = settings.MultiFrameCount;
		
		for (int i = 1; i <= framesToGen; ++i)
		{
			float factor = (float)i / (float)(framesToGen + 1);
			
			if (FrameGeneration::Instance().PresentGenerated(pSwapChain, 0, Flags, factor))
			{
				// [RESTORED UI RENDER ON GENERATED FRAME]
				ID3D11Texture2D* pBackBuffer = nullptr;
				pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
				if (pBackBuffer)
				{
					Present::Context->OMSetRenderTargets(1, &Present::RenderTargetView, NULL);
					ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
					pBackBuffer->Release();
				}

				// Present Generated Frame
				HRESULT hr = Present::Original(pSwapChain, 0, presentFlags);
				if (hr == DXGI_ERROR_INVALID_CALL && (presentFlags & 0x200)) {
					Present::Original(pSwapChain, 0, presentFlags & ~0x200);
				}
				
				UI::DebugOverlay::OnPresent(1);
				
				// [PACING FOR GENERATED FRAME]
				if (settings.FPSCap) g_Pacer.Wait(pacerFPS);
			}
		}

		// 5. Restore ORIGINAL Frame
		FrameGeneration::Instance().RestoreOriginal(pSwapChain);
	}

	ID3D11Texture2D* pBackBuffer = nullptr;
	pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
	if (pBackBuffer)
	{
		Present::Context->OMSetRenderTargets(1, &Present::RenderTargetView, NULL);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		pBackBuffer->Release();
	}
	
	// Present Real Frame
	HRESULT hr = Present::Original(pSwapChain, syncIntervalForReal, presentFlags);
	if (hr == DXGI_ERROR_INVALID_CALL && (presentFlags & 0x200)) {
		hr = Present::Original(pSwapChain, syncIntervalForReal, presentFlags & ~0x200);
	}
	UI::DebugOverlay::OnPresent(1);

	// [PACING FOR REAL FRAME]
	if (isEnabled && settings.FPSCap) g_Pacer.Wait(pacerFPS);

	// Update return time
	lastReturnTime = std::chrono::high_resolution_clock::now();

	return hr;
}

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

	if (true && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;

	return CallWindowProc(Present::OriginalWndProc, hWnd, uMsg, wParam, lParam);
}