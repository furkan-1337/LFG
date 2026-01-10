#include <wrl/client.h>
#include "../OpticalFlow/OpticalFlow.h"
#include "../Interpolation/FrameInterpolation.h"

using Microsoft::WRL::ComPtr;

class FrameGeneration
{
public:
	static FrameGeneration& Instance();

	// Settings Structure
	struct FrameGenSettings
	{
		// --- System & Core ---
		bool EnableAsyncCompute = false;
		bool LowLatencyMode = false;
		bool DisableVSync = true;
		
		// --- FPS Control ---
		bool FPSCap = false;
		int TargetFPS = 0; // 0 = Unlimited
		enum class FpsCapMode { Native, Display };
		FpsCapMode CapMode = FpsCapMode::Native;

		// --- Generation Control ---
		int MultiFrameCount = 1; // 1 = 2x FPS (1 Gen), 2 = 3x FPS (2 Gen), etc.
		bool EnableDynamicRatio = false;
		bool EnableAggressiveDynamicMode = false; // [Aggressive] Allow up to 10x generation
		int DynamicTargetFPS = 240; // Target FPS for Dynamic Ratio calculation

		// --- Resolution & Upscaling ---
		float RenderScale = 0.67f; // 0.5 - 1.0 - Balanced: 0.67f
		enum class UpscaleType { Native = 0, Nearest = 1, Bilinear = 2, Bicubic = 3, Lanczos = 4 };
		UpscaleType UpscaleMode = UpscaleType::Bicubic; // Balanced: Bicubic
		int LanczosRadius = 2; // Default 2

		// --- Optical Flow ---
		int OpticalFlowAlgorithm = 1; // 0=BlockMatching, 1=Farneback, 2=DIS - Balanced: Farneback
		int BlockSize = 16;
		int SearchRadius = 16; // Balanced: 16
		int MaxPyramidLevel = 1; // Start Level (0=Full, 1=Half, 2=Quarter) - Balanced: 1
		int MinPyramidLevel = 0; // End Level (0=Full, 1=Half...) - Balanced: 0
		
		bool EnableBiDirFlow = false; // Balanced: False
		bool EnableAdaptiveBlock = true; // Balanced: True
		bool EnableSubPixel = true; // Balanced: True
		float MotionSensitivity = 1.0f; // Multiplier for Optical Flow or Debug View scale

		// --- Post-Processing & Quality ---
		float RcasStrength = 0.5f; // Balanced: 0.5
		float GhostingReduction = 0.3f; // Balanced: 0.3
		bool EnableEdgeProtection = true; // Balanced: True
		bool EnableMotionSmoothing = false; // Balanced: False
		int SceneChangeThreshold = 1000; // > 0 to enable

		// --- Debug & Telemetry ---
		bool ShowDebugOverlay = true;
		int DebugViewMode = 0; // 0=Off, 1=Motion, 2=Mask
		float HUDThreshold = 0.01f;
		
		bool EnableSplitScreen = false;
		float SplitScreenPosition = 0.5f; // 0.0 - 1.0
	};

	void Initialize(ID3D11Device* device);
	void Capture(IDXGISwapChain* swapChain);
	
	void SetEnabled(bool enabled) { m_IsEnabled = enabled; }
	bool IsEnabled() const { return m_IsEnabled; }

    float GetLastGenerationTime() const { return m_LastGenTime; }

	void SetSettings(const FrameGenSettings& settings) { m_Settings = settings; }
	FrameGenSettings& GetSettings() { return m_Settings; }
	
	// Injects the generated frame into the swapchain
	bool PresentGenerated(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags, float factor);
	// Restores the originally captured frame to the swapchain
	void RestoreOriginal(IDXGISwapChain* swapChain);
	void Release();

	ID3D11Texture2D* GetCurrentTexture() const { return m_TexCurrent.Get(); }
	ID3D11Texture2D* GetPrevTexture() const { return m_TexPrev.Get(); }
	ID3D11Texture2D* GetMotionTexture() const { return m_TexMotion.Get(); }
	ID3D11Texture2D* GetGeneratedTexture() const { return m_TexGenerated.Get(); }

private:
	FrameGeneration() = default;
	~FrameGeneration() = default;
	
	// Helper for scaling
	void DispatchScale(ID3D11Texture2D* input, ID3D11Texture2D* output);

	ComPtr<ID3D11Device> m_Device;
	ComPtr<ID3D11DeviceContext> m_Context;
	ComPtr<ID3D11DeviceContext> m_DeferredContext; // [Async Compute]
	
	// Resources
	ComPtr<ID3D11Texture2D> m_TexCurrent;   // The frame copied from the game
	ComPtr<ID3D11Texture2D> m_TexPrev;      // The previous frame (for optical flow)
	ComPtr<ID3D11Texture2D> m_TexMotion;    // Motion vectors
	ComPtr<ID3D11Texture2D> m_TexGenerated; // The interpolated frame
	
	// Low Res Resources (for Performance Mode)
	ComPtr<ID3D11Texture2D> m_TexLowResCurrent;
	ComPtr<ID3D11Texture2D> m_TexLowResPrev;
	ComPtr<ID3D11Texture2D> m_TexLowResMotion;
	ComPtr<ID3D11Texture2D> m_TexLowResGenerated;
	
	struct CBUpscale
	{
		int Mode;
		int Radius;
		float InputWidth; // packed as float2 in shader
		float InputHeight; 
		// Padding handled by 16-byte alignment of next vector or explicit padding
		// HLSL: int, int, float2 = 8 + 8 = 16 bytes. Perfect.
	};
	ComPtr<ID3D11Buffer> m_cbUpscale;
	
	// Shaders
	ComPtr<ID3D11ComputeShader> m_csScale; // Now points to CS_Upscale

	// Subsystems
	OpticalFlow m_OpticalFlow;
	FrameInterpolation m_FrameInterpolation;

	bool m_IsEnabled = true;
    float m_LastGenTime = 0.0f;
	FrameGenSettings m_Settings;
};