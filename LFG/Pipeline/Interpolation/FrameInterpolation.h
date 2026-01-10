#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "../Processing/Sharpening.h"
#include "../Processing/EdgeDetection.h"

using Microsoft::WRL::ComPtr;

class FrameInterpolation
{
public:
	FrameInterpolation() = default;
	~FrameInterpolation() = default;

	bool Initialize(ID3D11Device* device, int width, int height);
	void Dispatch(ID3D11DeviceContext* context, 
		ID3D11Texture2D* texCurrent, 
		ID3D11Texture2D* texPrev, 
		ID3D11Texture2D* texMotion, 
		ID3D11Texture2D* texGenerated,
		ID3D11ShaderResourceView* statsSRV, // [Scene Change]
		float hudThreshold,
		int debugMode,
		float motionScale,
		float factor,
		int sceneThreshold,
		float rcasStrength,
		float ghostingStrength,
		bool enableEdgeProtection); // [Edge Detect]

	void DispatchSplitScreen(ID3D11DeviceContext* context,
		ID3D11Texture2D* texGen,
		ID3D11Texture2D* texReal,
		ID3D11Texture2D* output,
		float splitPos);

	void DispatchRCAS(ID3D11DeviceContext* context,
		ID3D11Texture2D* input,
		ID3D11Texture2D* output,
		float strength);

	ID3D11Texture2D* GetTempTexture() const { return m_TexSharpened.Get(); }

private:
	ComPtr<ID3D11ComputeShader> m_csHUDMask;
	ComPtr<ID3D11ComputeShader> m_csInterpolate;
	ComPtr<ID3D11ComputeShader> m_csDebugView;
	ComPtr<ID3D11ComputeShader> m_csSplitScreen; // [Split Screen]
	
    // Sub-systems
    Sharpening m_Sharpening;
    EdgeDetection m_EdgeDetection;

	// Resources
	ComPtr<ID3D11Texture2D> m_TexHUDMask; // R8_UNORM
	ComPtr<ID3D11Texture2D> m_TexSharpened; 
	
	ComPtr<ID3D11Buffer> m_cbHUD;
	ComPtr<ID3D11Buffer> m_cbDebug;
	ComPtr<ID3D11Buffer> m_cbFactor;
	ComPtr<ID3D11Buffer> m_cbSplit; // [Split Screen]

	struct CBDebug {
		int Mode;
		float Scale;
		int Padding[2];
	};
	struct CBHUD {
		float Threshold;
		int UseEdgeDetect; // [Edge Detect]
		float Padding[2];
	};
	struct CBFactor {
		float Factor;
		int SceneChangeThreshold;
		float GhostingStrength;
		float Padding;
	};
	struct CBSplit {
		float SplitPos;
		float Padding[3];
	};
};
