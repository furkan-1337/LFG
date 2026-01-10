#include "OpticalFlow.h"
#include <Pipeline/Shaders/Shader.h>
#include <Debug/Debug.h>
#include <Pipeline/Shaders/EmbeddedShaders.h>

#include <cmath>

bool OpticalFlow::Initialize(ID3D11Device* device, int width, int height)
{
	// 1. Load Compute Shaders
	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_Downsample, "CSMain", &m_csDownsample))
	{
		Debug::Error("Failed to load Downsample Shader");
		return false;
	}

	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_Upsample, "CSMain", &m_csUpsample))
	{
		Debug::Error("Failed to load Upsample Shader");
		return false;
	}

	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_BlockMatching, "CSMain", &m_csBlockMatching))
	{
		Debug::Error("Failed to load BlockMatching Shader");
		return false;
	}

	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_MotionSmooth, "CSMain", &m_csMotionSmooth))
	{
		Debug::Error("Failed to load MotionSmooth Shader");
		return false;
	}

	// [Farneback & DIS] Shaders
	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_Farneback_Expansion, "CSMain", &m_csFarnebackExpansion) ||
		!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_Farneback_Flow, "CSMain", &m_csFarnebackFlow) ||
		!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_DIS_Flow, "CSMain", &m_csDISFlow))
	{
		Debug::Error("Failed to load Advanced Optical Flow Shaders");
		// return false; // Optional
	}

	// Create Temp Texture for Smoothing
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R16G16_FLOAT; // Motion Vector Format
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	device->CreateTexture2D(&desc, nullptr, &m_TexSmoothTemp);

	// [Farneback] Poly Expansion Textures (RGBA16_FLOAT for precision)
	D3D11_TEXTURE2D_DESC polyDesc = desc;
	polyDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	if (FAILED(device->CreateTexture2D(&polyDesc, nullptr, &m_TexPolyCurr)) ||
		FAILED(device->CreateTexture2D(&polyDesc, nullptr, &m_TexPolyPrev)))
	{
		Debug::Error("Failed to create Optical Flow Poly Textures");
	}

	// 2. Create Constant Buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(CBuffer);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	
	if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_ConstantBuffer)))
	{
		Debug::Error("Failed to create Optical Flow Constant Buffer");
		return false;
	}

	// [New] Variance CB
	cbDesc.ByteWidth = sizeof(CBVariance);
	if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_cbVariance)))
	{
		Debug::Error("Failed to create Variance Const Buffer");
	}

	// [New] Shaders
	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_BidirectionalConsistency, "main", &m_csBidirectionalConsistency))
	{
		Debug::Error("Failed to load Bi-Directional Shader");
	}
	if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_AdaptiveVariance, "main", &m_csAdaptiveVariance)) // Typo in plan filename fixed
	{
		Debug::Error("Failed to load Adaptive Variance Shader");
	}
	
	// Backward Motion
	D3D11_TEXTURE2D_DESC moDesc = {};
	moDesc.Width = width;
	moDesc.Height = height;
	moDesc.MipLevels = 1;
	moDesc.ArraySize = 1;
	moDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	moDesc.SampleDesc.Count = 1;
	moDesc.Usage = D3D11_USAGE_DEFAULT;
	moDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	device->CreateTexture2D(&moDesc, nullptr, &m_TexMotionBackward);

	// Variance Grid (Width/16)
	D3D11_TEXTURE2D_DESC varDesc = {};
	varDesc.Width = (UINT)ceil(width / 16.0f);
	varDesc.Height = (UINT)ceil(height / 16.0f);
	varDesc.MipLevels = 1;
	varDesc.ArraySize = 1;
	varDesc.Format = DXGI_FORMAT_R8_UNORM;
	varDesc.SampleDesc.Count = 1;
	varDesc.Usage = D3D11_USAGE_DEFAULT;
	varDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	device->CreateTexture2D(&varDesc, nullptr, &m_TexVarianceGrid);

	// 3. Initialize Pyramid (Fixed Level 1 for now)
	D3D11_TEXTURE2D_DESC pyramidDesc = {};
	pyramidDesc.Width = width / 2;
	pyramidDesc.Height = height / 2;
	pyramidDesc.MipLevels = 1;
	pyramidDesc.ArraySize = 1;
	pyramidDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 
	pyramidDesc.SampleDesc.Count = 1;
	pyramidDesc.Usage = D3D11_USAGE_DEFAULT;
	pyramidDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	
	device->CreateTexture2D(&pyramidDesc, nullptr, &m_TexCurrentLevel1);
	device->CreateTexture2D(&pyramidDesc, nullptr, &m_TexPrevLevel1);
	
	// Motion Textures for Pyramid
	D3D11_TEXTURE2D_DESC motionDesc = {};
	motionDesc.Width = width / 2;
	motionDesc.Height = height / 2;
	motionDesc.MipLevels = 1;
	motionDesc.ArraySize = 1;
	motionDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	motionDesc.SampleDesc.Count = 1;
	motionDesc.Usage = D3D11_USAGE_DEFAULT;
	motionDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	device->CreateTexture2D(&motionDesc, nullptr, &m_TexMotionLevel1);
	
	// Level 2 (1/4 Res)
	pyramidDesc.Width = width / 4;
	pyramidDesc.Height = height / 4;
	device->CreateTexture2D(&pyramidDesc, nullptr, &m_TexCurrentLevel2);
	device->CreateTexture2D(&pyramidDesc, nullptr, &m_TexPrevLevel2);

	motionDesc.Width = width / 4;
	motionDesc.Height = height / 4;
	device->CreateTexture2D(&motionDesc, nullptr, &m_TexMotionLevel2);
	
	motionDesc.Width = width;
	motionDesc.Height = height;
	device->CreateTexture2D(&motionDesc, nullptr, &m_TexMotionUpsampled);

	// [Scene Change Stats Buffer]
	D3D11_BUFFER_DESC bufDesc = {};
	bufDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	bufDesc.ByteWidth = 4; // Single uint
	bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufDesc.StructureByteStride = 4;
	
	if (SUCCEEDED(device->CreateBuffer(&bufDesc, nullptr, &m_GlobalStatsBuffer)))
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = 1;
		device->CreateUnorderedAccessView(m_GlobalStatsBuffer.Get(), &uavDesc, &m_GlobalStatsUAV);
		
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = 1;
		device->CreateShaderResourceView(m_GlobalStatsBuffer.Get(), &srvDesc, &m_GlobalStatsSRV);
	}

	Debug::Info("Pyramid Level 1 created (%dx%d)", pyramidDesc.Width, pyramidDesc.Height);

	Debug::Info("OpticalFlow system initialized (Resolution: %dx%d).", width, height);
	return true;
}

void CreateSRV(ID3D11Device* dev, ID3D11Texture2D* tex, ID3D11ShaderResourceView** ppSRV) {
	if (!tex) return;
	dev->CreateShaderResourceView(tex, nullptr, ppSRV);
}

void CreateUAV(ID3D11Device* dev, ID3D11Texture2D* tex, ID3D11UnorderedAccessView** ppUAV) {
	if (!tex) return;
	dev->CreateUnorderedAccessView(tex, nullptr, ppUAV);
}

void OpticalFlow::Dispatch(ID3D11DeviceContext* context, 
	ID3D11Texture2D* currentFrame, 
	ID3D11Texture2D* prevFrame, 
	ID3D11Texture2D* outputMotion,
	int blockSize, int searchRadius,
	bool enableSubPixel, bool enableSmoothing, int maxLevel, int minLevel,
	FlowAlgorithm algo)
{
	if (!m_csDownsample || !m_csBlockMatching || !m_ConstantBuffer) return;
	if (!currentFrame || !prevFrame || !outputMotion) return;

	ID3D11Device* dev = nullptr;
	context->GetDevice(&dev);

	// Clear Stats Buffer
	if (m_GlobalStatsUAV)
	{
		UINT clearVals[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(m_GlobalStatsUAV.Get(), clearVals);
	}

	// Update Constants
	D3D11_TEXTURE2D_DESC texDesc;
	currentFrame->GetDesc(&texDesc);
	
	CBuffer cbData;
	cbData.Width = texDesc.Width;
	cbData.Height = texDesc.Height;
	cbData.BlockSize = blockSize;
	cbData.SearchRadius = searchRadius;
	cbData.EnableSubPixel = enableSubPixel ? 1 : 0;
	cbData.UseInitMotion = 0; // Default off
	
	context->UpdateSubresource(m_ConstantBuffer.Get(), 0, nullptr, &cbData, 0, 0);
	context->CSSetConstantBuffers(0, 1, m_ConstantBuffer.GetAddressOf());

	// [Algorithm Selection]
	if (algo == FlowAlgorithm::Farneback && m_csFarnebackExpansion && m_csFarnebackFlow)
	{
		// 1. Expansion Pass (Current & Prev)
		ComPtr<ID3D11ShaderResourceView> srvCurr, srvPrev;
		ComPtr<ID3D11UnorderedAccessView> uavPolyCurr, uavPolyPrev;
		CreateSRV(dev, currentFrame, &srvCurr);
		CreateSRV(dev, prevFrame, &srvPrev);
		CreateUAV(dev, m_TexPolyCurr.Get(), &uavPolyCurr);
		CreateUAV(dev, m_TexPolyPrev.Get(), &uavPolyPrev);

		// Expansion Dispatch (16x16 threads)
		D3D11_TEXTURE2D_DESC desc;
		currentFrame->GetDesc(&desc);
		UINT gx = (UINT)ceil(desc.Width / 16.0f);
		UINT gy = (UINT)ceil(desc.Height / 16.0f);

		context->CSSetShader(m_csFarnebackExpansion.Get(), nullptr, 0);
		context->CSSetShaderResources(0, 1, srvCurr.GetAddressOf());
		context->CSSetUnorderedAccessViews(0, 1, uavPolyCurr.GetAddressOf(), nullptr);
		context->Dispatch(gx, gy, 1);
		
		context->CSSetShaderResources(0, 1, srvPrev.GetAddressOf());
		context->CSSetUnorderedAccessViews(0, 1, uavPolyPrev.GetAddressOf(), nullptr);
		context->Dispatch(gx, gy, 1);

		// Unbind Expansion
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		context->CSSetShaderResources(0, 1, &nullSRV);

		// 2. Initial Guess via Hierarchical Block Matching (Reuse existing logic)
		if (maxLevel > 0)
		{
			Downsample(context, currentFrame, m_TexCurrentLevel1.Get());
			Downsample(context, prevFrame, m_TexPrevLevel1.Get());
			BlockMatching(context, m_TexCurrentLevel1.Get(), m_TexPrevLevel1.Get(), m_TexMotionLevel1.Get(), nullptr,
				blockSize / 2, searchRadius / 2, false);
			Upsample(context, m_TexMotionLevel1.Get(), m_TexMotionUpsampled.Get());
		}

		// 3. Farneback Flow (Refinement)
		ComPtr<ID3D11ShaderResourceView> srvPolyCurr, srvPolyPrev, srvInitMotion;
		ComPtr<ID3D11UnorderedAccessView> uavFlow;
		
		CreateSRV(dev, m_TexPolyCurr.Get(), &srvPolyCurr);
		CreateSRV(dev, m_TexPolyPrev.Get(), &srvPolyPrev);

		if (maxLevel == 0) {
			// Default Block Matching for initialization if H-Search is off
			BlockMatching(context, currentFrame, prevFrame, outputMotion, nullptr, blockSize, searchRadius, false); 
			// Copy outputMotion -> init input for Farneback
			context->CopyResource(m_TexMotionUpsampled.Get(), outputMotion);
			CreateSRV(dev, m_TexMotionUpsampled.Get(), &srvInitMotion);
		} else {
			CreateSRV(dev, m_TexMotionUpsampled.Get(), &srvInitMotion); 
		}

		CreateUAV(dev, outputMotion, &uavFlow);

		context->CSSetShader(m_csFarnebackFlow.Get(), nullptr, 0);
		ID3D11ShaderResourceView* flowSRVs[] = { srvPolyCurr.Get(), srvPolyPrev.Get(), srvInitMotion.Get() };
		context->CSSetShaderResources(0, 3, flowSRVs);
		context->CSSetUnorderedAccessViews(0, 1, uavFlow.GetAddressOf(), nullptr);
		
		context->Dispatch(gx, gy, 1);

		// Unbind Flow
		ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, 3, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}
	else if (algo == FlowAlgorithm::DIS && m_csDISFlow && m_csFarnebackExpansion)
	{
		// DIS Logic: Use Gradient of Prev Frame + Inverse Compositional
		ComPtr<ID3D11ShaderResourceView> srvPrev;
		ComPtr<ID3D11UnorderedAccessView> uavPolyPrev;
		CreateSRV(dev, prevFrame, &srvPrev);
		CreateUAV(dev, m_TexPolyPrev.Get(), &uavPolyPrev);
		
		D3D11_TEXTURE2D_DESC desc; // reuse desc
		currentFrame->GetDesc(&desc);
		UINT gx = (UINT)ceil(desc.Width / 16.0f);
		UINT gy = (UINT)ceil(desc.Height / 16.0f);
		
		context->CSSetShader(m_csFarnebackExpansion.Get(), nullptr, 0);
		context->CSSetShaderResources(0, 1, srvPrev.GetAddressOf());
		context->CSSetUnorderedAccessViews(0, 1, uavPolyPrev.GetAddressOf(), nullptr);
		context->Dispatch(gx, gy, 1);
		
		// Unbind
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		context->CSSetShaderResources(0, 1, &nullSRV);

		// 2. Initialization (Block Matching)
		if (maxLevel > 0)
		{
			Downsample(context, currentFrame, m_TexCurrentLevel1.Get());
			Downsample(context, prevFrame, m_TexPrevLevel1.Get());
			BlockMatching(context, m_TexCurrentLevel1.Get(), m_TexPrevLevel1.Get(), m_TexMotionLevel1.Get(), nullptr, blockSize/2, searchRadius/2, false);
			Upsample(context, m_TexMotionLevel1.Get(), m_TexMotionUpsampled.Get());
		}
		else
		{
			BlockMatching(context, currentFrame, prevFrame, outputMotion, nullptr, blockSize, searchRadius, false);
			context->CopyResource(m_TexMotionUpsampled.Get(), outputMotion);
		}

		// 3. DIS Flow (Gradient Descent Refinement)
		ComPtr<ID3D11ShaderResourceView> srvCurrRaw, srvPrevRaw, srvPolyPrev, srvInitMotion;
		ComPtr<ID3D11UnorderedAccessView> uavFlow;
		CreateSRV(dev, currentFrame, &srvCurrRaw);
		CreateSRV(dev, prevFrame, &srvPrevRaw); // Need Raw Prev too
		CreateSRV(dev, m_TexPolyPrev.Get(), &srvPolyPrev); // Gradients
		CreateSRV(dev, m_TexMotionUpsampled.Get(), &srvInitMotion);
		CreateUAV(dev, outputMotion, &uavFlow);

		context->CSSetShader(m_csDISFlow.Get(), nullptr, 0);
		ID3D11ShaderResourceView* disSRVs[] = { srvCurrRaw.Get(), srvPrevRaw.Get(), srvPolyPrev.Get(), srvInitMotion.Get() };
		context->CSSetShaderResources(0, 4, disSRVs);
		context->CSSetUnorderedAccessViews(0, 1, uavFlow.GetAddressOf(), nullptr);
		
		context->Dispatch(gx, gy, 1);
		
		// Unbind
		ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, 4, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}
	else
	{
		// Resources Map
		ID3D11Texture2D* texCurr[3] = { currentFrame, m_TexCurrentLevel1.Get(), m_TexCurrentLevel2.Get() };
		ID3D11Texture2D* texPrev[3] = { prevFrame, m_TexPrevLevel1.Get(), m_TexPrevLevel2.Get() };
		ID3D11Texture2D* texMotion[3] = { outputMotion, m_TexMotionLevel1.Get(), m_TexMotionLevel2.Get() };
		
		// Valid Range Check
		if (maxLevel > 2) maxLevel = 2;
		if (minLevel < 0) minLevel = 0;
		if (minLevel > maxLevel) minLevel = maxLevel;

		// 1. Downsample Chain (Need to fill levels down to maxLevel)
		// If maxLevel is 0, no downsampling needed.
		// If maxLevel is 1, need L0->L1.
		// If maxLevel is 2, need L0->L1->L2.
		for (int l = 1; l <= maxLevel; ++l)
		{
			Downsample(context, texCurr[l-1], texCurr[l]);
			Downsample(context, texPrev[l-1], texPrev[l]);
		}
		
		// Manual Implementation for robustness
		
		// Level 2 (Quarter)
		if (maxLevel >= 2 && minLevel <= 2)
		{
			// Calc L2
			// Init = Null (Coarsest)
			int blk = blockSize / 4; if (blk < 4) blk = 4;
			int rad = searchRadius / 4; if (rad < 2) rad = 2;
			
			BlockMatching(context, texCurr[2], texPrev[2], texMotion[2], nullptr, blk, rad, false); // u0 = MotionL2
		}
		
		// Level 1 (Half)
		if (maxLevel >= 1 && minLevel <= 1)
		{
			ID3D11Texture2D* init = nullptr;
			
			if (maxLevel >= 2) // If coming from L2
			{
				// Upsample L2 -> L1
				Upsample(context, texMotion[2], texMotion[1]); 
				init = texMotion[1];

				// Impl:
				if (maxLevel == 2)
				{
					int blk = blockSize/4; if (blk<4) blk=4;
					BlockMatching(context, texCurr[2], texPrev[2], texMotion[2], nullptr, blk, searchRadius/4, false);
					
					if (minLevel == 2) {
						// L2 -> L0
						Upsample(context, texMotion[2], outputMotion); // Direct upsample to output
						return;
					}
					
					Upsample(context, texMotion[2], texMotion[1]);
				}
				else if (maxLevel == 1)
				{
					int blk = blockSize/2; if (blk<4) blk=4;
					BlockMatching(context, texCurr[1], texPrev[1], texMotion[1], nullptr, blk, searchRadius/2, false);
				}
				
				// Level 1 (Result in texMotion[1]).
				if (minLevel == 1)
				{
					// L1 -> L0
					Upsample(context, texMotion[1], outputMotion);
					return;
				}
				
				// Level 0 (Full)
				Upsample(context, texMotion[1], m_TexMotionUpsampled.Get());
				
				// Calc L0
				BlockMatching(context, texCurr[0], texPrev[0], texMotion[0], m_TexMotionUpsampled.Get(), blockSize, searchRadius, enableSubPixel);
			}
		}
	// 5. Motion Smoothing (Optional)
	if (enableSmoothing && m_TexSmoothTemp)
	{
		// Copy Output -> Temp
		context->CopyResource(m_TexSmoothTemp.Get(), outputMotion);
		
		ComPtr<ID3D11ShaderResourceView> srvInput;
		ComPtr<ID3D11UnorderedAccessView> uavOutput;
		CreateSRV(dev, m_TexSmoothTemp.Get(), &srvInput);
		CreateUAV(dev, outputMotion, &uavOutput); // Write back to Output

		context->CSSetShader(m_csMotionSmooth.Get(), nullptr, 0);
		context->CSSetShaderResources(0, 1, srvInput.GetAddressOf());
		context->CSSetUnorderedAccessViews(0, 1, uavOutput.GetAddressOf(), nullptr);
		
		D3D11_TEXTURE2D_DESC texDesc;
		currentFrame->GetDesc(&texDesc);
		context->Dispatch((UINT)ceil(texDesc.Width / 8.0f), (UINT)ceil(texDesc.Height / 8.0f), 1);

		ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 1, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}
	
	dev->Release();
	}
}

void OpticalFlow::Downsample(ID3D11DeviceContext* context, ID3D11Texture2D* input, ID3D11Texture2D* output)
{
	if (!input || !output) return;

	ID3D11Device* dev = nullptr;
	context->GetDevice(&dev);

	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
	CreateSRV(dev, input, &srv);
	CreateUAV(dev, output, &uav);
	dev->Release();

	context->CSSetShader(m_csDownsample.Get(), nullptr, 0);
	context->CSSetShaderResources(0, 1, srv.GetAddressOf());
	context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);

	D3D11_TEXTURE2D_DESC desc;
	output->GetDesc(&desc);
	context->Dispatch((UINT)ceil(desc.Width / 8.0f), (UINT)ceil(desc.Height / 8.0f), 1);

	// Unbind
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);
}

void OpticalFlow::Upsample(ID3D11DeviceContext* context, ID3D11Texture2D* inputLowRes, ID3D11Texture2D* outputHighRes)
{
	if (!inputLowRes || !outputHighRes) return;

	ID3D11Device* dev = nullptr;
	context->GetDevice(&dev);

	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
	CreateSRV(dev, inputLowRes, &srv);
	CreateUAV(dev, outputHighRes, &uav);
	dev->Release();

	context->CSSetShader(m_csUpsample.Get(), nullptr, 0);
	context->CSSetShaderResources(0, 1, srv.GetAddressOf());
	context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);

	D3D11_TEXTURE2D_DESC desc;
	outputHighRes->GetDesc(&desc);
	context->Dispatch((UINT)ceil(desc.Width / 8.0f), (UINT)ceil(desc.Height / 8.0f), 1);

	// Unbind
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);
}

void OpticalFlow::BlockMatching(ID3D11DeviceContext* context, 
	ID3D11Texture2D* current, ID3D11Texture2D* prev, ID3D11Texture2D* motion, 
	ID3D11Texture2D* initMotion,
	int blockSize, int searchRadius, bool enableSubPixel)
{
	ID3D11Device* dev = nullptr;
	context->GetDevice(&dev);

	// Update CBuffer params for this pass
	D3D11_TEXTURE2D_DESC desc;
	current->GetDesc(&desc);

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(context->Map(m_ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		CBuffer* pData = (CBuffer*)mapped.pData;
		pData->Width = desc.Width;
		pData->Height = desc.Height;
		pData->BlockSize = blockSize;
		pData->SearchRadius = searchRadius;
		pData->EnableSubPixel = enableSubPixel ? 1 : 0;
		pData->UseInitMotion = (initMotion != nullptr) ? 1 : 0;
		context->Unmap(m_ConstantBuffer.Get(), 0);
	}
	// Bind CB
	context->CSSetConstantBuffers(0, 1, m_ConstantBuffer.GetAddressOf());

	ComPtr<ID3D11ShaderResourceView> srvCurrent, srvPrev, srvInit;
	ComPtr<ID3D11UnorderedAccessView> uavMotion;
	CreateSRV(dev, current, &srvCurrent);
	CreateSRV(dev, prev, &srvPrev);
	if (initMotion) CreateSRV(dev, initMotion, &srvInit);
	CreateUAV(dev, motion, &uavMotion);
	
	// Create common sampler (should be member to avoid recreation, but fine for now)
	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	ComPtr<ID3D11SamplerState> sampler;
	dev->CreateSamplerState(&sampDesc, &sampler);
	
	dev->Release();

	context->CSSetShader(m_csBlockMatching.Get(), nullptr, 0);
	ID3D11ShaderResourceView* srvs[] = { srvCurrent.Get(), srvPrev.Get(), srvInit.Get() };
	context->CSSetShaderResources(0, 3, srvs);
	ID3D11UnorderedAccessView* uavs[] = { uavMotion.Get(), m_GlobalStatsUAV.Get() }; // Slot 0: Motion, Slot 1: Stats
	context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
	context->CSSetSamplers(0, 1, sampler.GetAddressOf());

	context->Dispatch((UINT)ceil(desc.Width / 8.0f), (UINT)ceil(desc.Height / 8.0f), 1);

	// Unbind
	ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
	context->CSSetSamplers(0, 0, nullptr);
}

void OpticalFlow::DispatchBiDirectional(ID3D11DeviceContext* context,
		ID3D11Texture2D* currentFrame, 
		ID3D11Texture2D* prevFrame, 
		ID3D11Texture2D* outputMotion,
		int blockSize, int searchRadius)
{
	// 1. Calculate Forward Flow (Prev -> Curr)
	// Output: outputMotion
	BlockMatching(context, currentFrame, prevFrame, outputMotion, nullptr, blockSize, searchRadius, true);

	// 2. Calculate Backward Flow (Curr -> Prev)
	// Inputs swapped!
	// Output: m_TexMotionBackward
	if (m_TexMotionBackward)
	{
		BlockMatching(context, prevFrame, currentFrame, m_TexMotionBackward.Get(), nullptr, blockSize, searchRadius, true);
	}

	// 3. Consistency Check (Fusion)
	// Output: outputMotion (Refined)
	if (m_csBidirectionalConsistency)
	{
		CheckConsistency(context, outputMotion, m_TexMotionBackward.Get(), outputMotion);
	}
}

void OpticalFlow::DispatchAdaptive(ID3D11DeviceContext* context, 
		ID3D11Texture2D* currentFrame, 
		ID3D11Texture2D* prevFrame, 
		ID3D11Texture2D* outputMotion,
		int searchRadius)
{
	if (!m_TexVarianceGrid || !m_csAdaptiveVariance) return;

	// 1. Calculate Variance Grid
	CalcVariance(context, currentFrame, m_TexVarianceGrid.Get());

	// 2. Block Matching with Adaptive Variance
	// For now, running standard BlockMatching 16x16.
	// Future improvement: Run 2 passes (16x16 and 8x8) and blend?
	// Or trust that the Shader is updated (we didn't update BlockMatching.hlsl yet).
	// Let's stick to base implementation for now.
	BlockMatching(context, currentFrame, prevFrame, outputMotion, nullptr, 16, searchRadius, true);
}

void OpticalFlow::CalcVariance(ID3D11DeviceContext* context, ID3D11Texture2D* input, ID3D11Texture2D* outputVar)
{
	if (!input || !outputVar || !m_csAdaptiveVariance) return;
	
	ID3D11Device* dev = nullptr;
	context->GetDevice(&dev);

	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
	CreateSRV(dev, input, &srv);
	CreateUAV(dev, outputVar, &uav);
	dev->Release();

	context->CSSetShader(m_csAdaptiveVariance.Get(), nullptr, 0);
	context->CSSetShaderResources(0, 1, srv.GetAddressOf());
	context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
	
	D3D11_TEXTURE2D_DESC desc;
	outputVar->GetDesc(&desc);
	context->Dispatch((UINT)ceil(desc.Width / 8.0f), (UINT)ceil(desc.Height / 8.0f), 1);

	// Unbind
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);
}

void OpticalFlow::CheckConsistency(ID3D11DeviceContext* context, ID3D11Texture2D* fwd, ID3D11Texture2D* bwd, ID3D11Texture2D* output)
{
	if (!fwd || !bwd || !output) return;

	ID3D11Device* dev = nullptr;
	context->GetDevice(&dev);

	ComPtr<ID3D11ShaderResourceView> srvFwd, srvBwd;
	ComPtr<ID3D11UnorderedAccessView> uavOut; 
	
	CreateSRV(dev, fwd, &srvFwd);
	CreateSRV(dev, bwd, &srvBwd);
	
	if (!m_TexSmoothTemp) return; 
	CreateUAV(dev, m_TexSmoothTemp.Get(), &uavOut);
	
	context->CSSetShader(m_csBidirectionalConsistency.Get(), nullptr, 0);
	ID3D11ShaderResourceView* srvs[] = { srvFwd.Get(), srvBwd.Get() };
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavOut.GetAddressOf(), nullptr); 
	
	D3D11_TEXTURE2D_DESC desc;
	fwd->GetDesc(&desc);
	context->Dispatch((UINT)ceil(desc.Width / 16.0f), (UINT)ceil(desc.Height / 16.0f), 1);
	
	// Unbind
	ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 2, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	
	// Copy Temp -> Output
	context->CopyResource(output, m_TexSmoothTemp.Get());
	
	dev->Release();
}

