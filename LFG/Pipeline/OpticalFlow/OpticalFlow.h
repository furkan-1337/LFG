#include <d3d11.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

enum class FlowAlgorithm {
	BlockMatching = 0,
	Farneback = 1,
	DIS = 2
};

class OpticalFlow
{
public:
	OpticalFlow() = default;
	~OpticalFlow() = default;

	bool Initialize(ID3D11Device* device, int width, int height);
	void Dispatch(ID3D11DeviceContext* context, 
		ID3D11Texture2D* currentFrame, 
		ID3D11Texture2D* prevFrame, 
		ID3D11Texture2D* outputMotion,
		int blockSize, int searchRadius,
		bool enableSubPixel, bool enableSmoothing, 
		int maxLevel, int minLevel,
		FlowAlgorithm algo = FlowAlgorithm::BlockMatching);
		
	// Advanced Features
	void DispatchBiDirectional(ID3D11DeviceContext* context,
		ID3D11Texture2D* currentFrame, 
		ID3D11Texture2D* prevFrame, 
		ID3D11Texture2D* outputMotion,
		int blockSize, int searchRadius);

	void DispatchAdaptive(ID3D11DeviceContext* context, 
		ID3D11Texture2D* currentFrame, 
		ID3D11Texture2D* prevFrame, 
		ID3D11Texture2D* outputMotion,
		int searchRadius);

private:
	// Implementation of Hierarchical Search
	void Downsample(ID3D11DeviceContext* context, ID3D11Texture2D* input, ID3D11Texture2D* output);
	void Upsample(ID3D11DeviceContext* context, ID3D11Texture2D* inputLowRes, ID3D11Texture2D* outputHighRes);
	void BlockMatching(ID3D11DeviceContext* context, 
		ID3D11Texture2D* current, ID3D11Texture2D* prev, ID3D11Texture2D* motion, 
		ID3D11Texture2D* initMotion,
		int blockSize, int searchRadius, bool enableSubPixel);
		
	// New Implementation for Adaptive
	void CalcVariance(ID3D11DeviceContext* context, ID3D11Texture2D* input, ID3D11Texture2D* outputVar);
	void CheckConsistency(ID3D11DeviceContext* context, ID3D11Texture2D* fwd, ID3D11Texture2D* bwd, ID3D11Texture2D* output);

	ComPtr<ID3D11ComputeShader> m_csDownsample;
	ComPtr<ID3D11ComputeShader> m_csUpsample;
	ComPtr<ID3D11ComputeShader> m_csBlockMatching;
	
	// New Shaders
	ComPtr<ID3D11ComputeShader> m_csBidirectionalConsistency;
	ComPtr<ID3D11ComputeShader> m_csAdaptiveVariance;
	
	// Helper to store pyramid levels
	std::vector<ComPtr<ID3D11Texture2D>> m_PyramidLevels;
	
	struct CBuffer
	{
		int Width;
		int Height;
		int BlockSize;
		int SearchRadius;
		int EnableSubPixel;
		int UseInitMotion;
		int Padding[2];
	};
	
	struct CBVariance {
		float Threshold; // For Adaptive
		float Padding[3];
	};
	
	ComPtr<ID3D11Buffer> m_ConstantBuffer;
	ComPtr<ID3D11Buffer> m_cbVariance; // New CB
	
	ComPtr<ID3D11ComputeShader> m_csMotionSmooth;
	ComPtr<ID3D11Texture2D> m_TexSmoothTemp; // Temp buffer for smoothing
	
	// Resources for BiDir/Adaptive
	ComPtr<ID3D11Texture2D> m_TexMotionBackward; // for BiDir
	ComPtr<ID3D11Texture2D> m_TexVarianceGrid;   // for Adaptive
	
	// Hierarchy Resources
	ComPtr<ID3D11Texture2D> m_TexMotionLevel1; // Half-res motion
	ComPtr<ID3D11Texture2D> m_TexMotionUpsampled; // Upsampled motion for init
	ComPtr<ID3D11Texture2D> m_TexCurrentLevel1; // Half-res current frame
	ComPtr<ID3D11Texture2D> m_TexPrevLevel1; // Half-res prev frame
	
	// Level 2 (1/4 Res)
	ComPtr<ID3D11Texture2D> m_TexMotionLevel2; 
	ComPtr<ID3D11Texture2D> m_TexCurrentLevel2; 
	ComPtr<ID3D11Texture2D> m_TexPrevLevel2;
	
	// Scene Change Stats
	ComPtr<ID3D11Buffer> m_GlobalStatsBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_GlobalStatsUAV;
	ComPtr<ID3D11ShaderResourceView> m_GlobalStatsSRV;
	
	// Advanced Optical Flow
	ComPtr<ID3D11ComputeShader> m_csFarnebackExpansion;
	ComPtr<ID3D11ComputeShader> m_csFarnebackFlow;
	ComPtr<ID3D11ComputeShader> m_csDISFlow;
	
	ComPtr<ID3D11Texture2D> m_TexPolyCurr; // Coeffs for Current Frame
	ComPtr<ID3D11Texture2D> m_TexPolyPrev; // Coeffs for Previous Frame

public:
	ID3D11ShaderResourceView* GetStatsSRV() const { return m_GlobalStatsSRV.Get(); }
	ID3D11Texture2D* GetVarianceGrid() const { return m_TexVarianceGrid.Get(); } // Expose for debugging if needed
};
