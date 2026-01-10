#include "FrameInterpolation.h"
#include "../Shaders/Shader.h"
#include <Debug/Debug.h>
#include <Pipeline/Shaders/EmbeddedShaders.h>
#include <cmath>

// Helper (Duplicated from OpticalFlow, ideally move to Utils)
static void CreateSRV(ID3D11Device* dev, ID3D11Texture2D* tex, ID3D11ShaderResourceView** ppSRV) {
	if (!tex) return;
	dev->CreateShaderResourceView(tex, nullptr, ppSRV);
}

static void CreateUAV(ID3D11Device* dev, ID3D11Texture2D* tex, ID3D11UnorderedAccessView** ppUAV) {
	if (!tex) return;
	dev->CreateUnorderedAccessView(tex, nullptr, ppUAV);
}

bool FrameInterpolation::Initialize(ID3D11Device* device, int width, int height)
{
	// 1. Load Shaders
	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_HUDMask, "CSMain", &m_csHUDMask))
	{
		Debug::Error("Failed to load HUDMask Shader");
		return false;
	}

	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_Interpolate, "CSMain", &m_csInterpolate))
	{
		Debug::Error("Failed to load Interpolate Shader");
		return false;
	}

	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_DebugView, "CSMain", &m_csDebugView))
	{
		Debug::Error("Failed to load DebugView Shader");
		return false;
	}

	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_SplitScreen, "main", &m_csSplitScreen))
	{
		Debug::Error("Failed to load SplitScreen Shader");
		return false;
	}
    
    // [Processing Sub-systems]
    if (!m_Sharpening.Initialize(device))
    {
        Debug::Error("Failed to initialize Sharpening system");
        return false;
    }

    if (!m_EdgeDetection.Initialize(device, width, height))
    {
        Debug::Error("Failed to initialize Edge Detection system");
        // Non-fatal?
    }

	// 2. Create HUD Mask Texture (R8_UNORM)
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8_UNORM; // Single channel mask
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &m_TexHUDMask)))
	{
		Debug::Error("Failed to create HUD Mask Texture");
		return false;
	}

	// [RCAS] Sharpened Temp Texture (RGBA8/RGBA16F)
	// Must match the Output format of the Swapchain/Generator (Usually RGBA8 for swapchain)
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &m_TexSharpened)))
	{
		Debug::Error("Failed to create RCAS Temp Texture");
		return false;
	}

	// 3. Create Constant Buffers
	{
		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.Usage = D3D11_USAGE_DEFAULT;
		cbDesc.ByteWidth = sizeof(CBHUD);
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		device->CreateBuffer(&cbDesc, nullptr, &m_cbHUD);

		cbDesc.ByteWidth = sizeof(CBDebug);
		device->CreateBuffer(&cbDesc, nullptr, &m_cbDebug);
		
		cbDesc.ByteWidth = sizeof(CBFactor);
		device->CreateBuffer(&cbDesc, nullptr, &m_cbFactor);

		cbDesc.ByteWidth = sizeof(CBSplit);
		device->CreateBuffer(&cbDesc, nullptr, &m_cbSplit);
	}

	Debug::Info("FrameInterpolation system initialized.");
	return true;
}

// Update Dispatch Signature
void FrameInterpolation::Dispatch(ID3D11DeviceContext* context, 
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
	bool enableEdgeProtection)
{
	ID3D11Device* dev = nullptr;
	context->GetDevice(&dev);

	D3D11_TEXTURE2D_DESC desc;
	texCurrent->GetDesc(&desc);
	UINT groupsX = (UINT)ceil(desc.Width / 8.0f); // 8x8 groups for most shaders
	UINT groupsY = (UINT)ceil(desc.Height / 8.0f);

	// Update CBuffers
	CBHUD cbHudData = { hudThreshold, enableEdgeProtection ? 1 : 0, {0,0} };
	context->UpdateSubresource(m_cbHUD.Get(), 0, nullptr, &cbHudData, 0, 0);

	CBDebug cbDebugData = { debugMode, motionScale, {0,0} };
	context->UpdateSubresource(m_cbDebug.Get(), 0, nullptr, &cbDebugData, 0, 0);
	
	CBFactor cbFactorData = { factor, sceneThreshold, ghostingStrength, 0.0f };
	context->UpdateSubresource(m_cbFactor.Get(), 0, nullptr, &cbFactorData, 0, 0);

	// ---------------------------------------------------------
	// Pass 0: Edge Detection (If Enabled)
	// ---------------------------------------------------------
	if (enableEdgeProtection)
	{
        m_EdgeDetection.Dispatch(context, texCurrent);
	}

	// ---------------------------------------------------------
	// Pass 1: HUD Mask Generatation
	// ---------------------------------------------------------
	{
		ComPtr<ID3D11ShaderResourceView> srvCurr, srvPrev, srvEdge; 
		ComPtr<ID3D11UnorderedAccessView> uavMask;

		CreateSRV(dev, texCurrent, &srvCurr);
		CreateSRV(dev, texPrev, &srvPrev);
		CreateUAV(dev, m_TexHUDMask.Get(), &uavMask);
        
		if (enableEdgeProtection) CreateSRV(dev, m_EdgeDetection.GetOutputTexture(), &srvEdge); 

		context->CSSetShader(m_csHUDMask.Get(), nullptr, 0);
		ID3D11ShaderResourceView* srvs[] = { srvCurr.Get(), srvPrev.Get(), srvEdge.Get() }; 
		context->CSSetShaderResources(0, 3, srvs); 
		context->CSSetUnorderedAccessViews(0, 1, uavMask.GetAddressOf(), nullptr);
		context->CSSetConstantBuffers(0, 1, m_cbHUD.GetAddressOf());

		context->Dispatch(groupsX, groupsY, 1);
		
		// Unbind
		ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 3, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}

	// ---------------------------------------------------------
	// Pass 2: Main Interpolation
	// ---------------------------------------------------------
	if (debugMode > 0)
	{
		// DEBUG VIEW
		ComPtr<ID3D11ShaderResourceView> srvMotion, srvMask;
		ComPtr<ID3D11UnorderedAccessView> uavOut;

		CreateSRV(dev, texMotion, &srvMotion);
		CreateSRV(dev, m_TexHUDMask.Get(), &srvMask);
		CreateUAV(dev, texGenerated, &uavOut);

		context->CSSetShader(m_csDebugView.Get(), nullptr, 0);
		ID3D11ShaderResourceView* srvs[] = { srvMotion.Get(), srvMask.Get() };
		context->CSSetShaderResources(0, 2, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavOut.GetAddressOf(), nullptr);
		context->CSSetConstantBuffers(0, 1, m_cbDebug.GetAddressOf());

		context->Dispatch(groupsX, groupsY, 1);

		// Unbind
		ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		ID3D11Buffer* nullCB = nullptr;
		context->CSSetShaderResources(0, 2, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, &nullCB);
	}
	else
	{
		// STANDARD INTERPOLATION
		bool useRCAS = (rcasStrength > 0.0f) && m_TexSharpened;
		
		ComPtr<ID3D11ShaderResourceView> srvCurr, srvPrev, srvMotion, srvMask;
		ComPtr<ID3D11UnorderedAccessView> uavGen;
		
		CreateSRV(dev, texCurrent, &srvCurr);
		CreateSRV(dev, texPrev, &srvPrev);
		CreateSRV(dev, texMotion, &srvMotion);
		CreateSRV(dev, m_TexHUDMask.Get(), &srvMask);
		
		// If RCAS is ON, we write to the TEMP buffer first.
		// If RCAS is OFF, we write directly to the OUTPUT buffer.
		if (useRCAS) {
			// Debug::Info("Dispatching Interpolation to Temp Buffer (RCAS Active: %.2f)", rcasStrength); 
			CreateUAV(dev, m_TexSharpened.Get(), &uavGen);
		} else {
			// Debug::Info("Dispatching Interpolation to Final Buffer (RCAS Off: %.2f)", rcasStrength);
			CreateUAV(dev, texGenerated, &uavGen);
		}

		context->CSSetShader(m_csInterpolate.Get(), nullptr, 0);
		ID3D11ShaderResourceView* srvs[] = { srvCurr.Get(), srvPrev.Get(), srvMotion.Get(), srvMask.Get(), statsSRV };
		context->CSSetShaderResources(0, 5, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavGen.GetAddressOf(), nullptr);
		context->CSSetConstantBuffers(0, 1, m_cbFactor.GetAddressOf()); // Bind Factor
		
		// Samplers...
		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		ComPtr<ID3D11SamplerState> sampler;
		dev->CreateSamplerState(&sampDesc, &sampler);
		context->CSSetSamplers(0, 1, sampler.GetAddressOf());

		context->Dispatch(groupsX, groupsY, 1);

		// Unbind Interpolation
		ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr, nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 5, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		context->CSSetSamplers(0, 0, nullptr);
		
		// [RCAS PASS]
		if (useRCAS)
		{
            m_Sharpening.Dispatch(context, m_TexSharpened.Get(), texGenerated, rcasStrength);
		}
	}
	
	dev->Release();
}

void FrameInterpolation::DispatchSplitScreen(ID3D11DeviceContext* context, 
		ID3D11Texture2D* texGen, 
		ID3D11Texture2D* texReal, 
		ID3D11Texture2D* output, 
		float splitPos)
{
	if (!m_csSplitScreen) return;

	ID3D11Device* dev = nullptr;
	context->GetDevice(&dev);

	D3D11_TEXTURE2D_DESC desc;
	texGen->GetDesc(&desc);
	UINT groupsX = (UINT)ceil(desc.Width / 16.0f);
	UINT groupsY = (UINT)ceil(desc.Height / 16.0f);

	// Update CB
	CBSplit cbData = { splitPos, {0,0,0} };
	context->UpdateSubresource(m_cbSplit.Get(), 0, nullptr, &cbData, 0, 0);

	ComPtr<ID3D11ShaderResourceView> srvGen, srvReal;
	ComPtr<ID3D11UnorderedAccessView> uavOut;

	CreateSRV(dev, texGen, &srvGen);
	CreateSRV(dev, texReal, &srvReal);
	CreateUAV(dev, output, &uavOut);

	context->CSSetShader(m_csSplitScreen.Get(), nullptr, 0);
	ID3D11ShaderResourceView* srvs[] = { srvGen.Get(), srvReal.Get() };
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavOut.GetAddressOf(), nullptr);
	context->CSSetConstantBuffers(0, 1, m_cbSplit.GetAddressOf());

	context->Dispatch(groupsX, groupsY, 1);

	// Unbind
	ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ID3D11Buffer* nullCB = nullptr;

	context->CSSetShaderResources(0, 2, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetConstantBuffers(0, 1, &nullCB);

	dev->Release();
}

void FrameInterpolation::DispatchRCAS(ID3D11DeviceContext* context, 
    ID3D11Texture2D* input, 
    ID3D11Texture2D* output, 
    float strength)
{
    m_Sharpening.Dispatch(context, input, output, strength);
}


