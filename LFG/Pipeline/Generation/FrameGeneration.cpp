#include "FrameGeneration.h"
#include <Debug/Debug.h>
#include <Pipeline/Shaders/EmbeddedShaders.h>
#include <Pipeline/Shaders/Shader.h>

FrameGeneration& FrameGeneration::Instance()
{
	static FrameGeneration instance;
	return instance;
}

void FrameGeneration::Initialize(ID3D11Device* device)
{
	m_Device = device;
	m_Device->GetImmediateContext(&m_Context);

	// Load Shaders
    // [New] Scale Shader (Renamed to CS_Upscale.hlsl)
    if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_Upscale, "main", &m_csScale))
    {
        Debug::Error("Failed to load CS_Upscale shader");
    }

    // Create Constant Buffer for Upscale
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(CBUpscale);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_Device->CreateBuffer(&cbDesc, nullptr, &m_cbUpscale);

	// [Async Compute] 
	// Initialize Deferred Context for batching compute commands.
	HRESULT hr = m_Device->CreateDeferredContext(0, &m_DeferredContext);
	if (FAILED(hr)) {
		Debug::Error("Failed to create Deferred Context (0x%X)", hr);
	} else {
		Debug::Info("Deferred Context created successfully.");
	}
	
	Debug::Info("Frame Generation initialized.");
}

void FrameGeneration::DispatchScale(ID3D11Texture2D* input, ID3D11Texture2D* output)
{
    if (!input || !output || !m_csScale || !m_cbUpscale) return;

    D3D11_TEXTURE2D_DESC inDesc;
    input->GetDesc(&inDesc);
    D3D11_TEXTURE2D_DESC outDesc;
    output->GetDesc(&outDesc);

    ID3D11ShaderResourceView* nullSRVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    m_Context->CSSetShaderResources(0, 8, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
    m_Context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    // Update Constant Buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_Context->Map(m_cbUpscale.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        CBUpscale* pData = (CBUpscale*)mapped.pData;
        pData->Mode = (int)m_Settings.UpscaleMode;
        pData->Radius = m_Settings.LanczosRadius;
        pData->InputWidth = (float)inDesc.Width;
        pData->InputHeight = (float)inDesc.Height;
        m_Context->Unmap(m_cbUpscale.Get(), 0);
    }
    m_Context->CSSetConstantBuffers(0, 1, m_cbUpscale.GetAddressOf());

    // Create Views
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    
    // Linear
	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	ComPtr<ID3D11SamplerState> linearSampler;
	m_Device->CreateSamplerState(&sampDesc, &linearSampler);

    // Point
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    ComPtr<ID3D11SamplerState> pointSampler;
    m_Device->CreateSamplerState(&sampDesc, &pointSampler);

    m_Device->CreateShaderResourceView(input, nullptr, &srv);
    m_Device->CreateUnorderedAccessView(output, nullptr, &uav);

    m_Context->CSSetShader(m_csScale.Get(), nullptr, 0);
    m_Context->CSSetShaderResources(0, 1, srv.GetAddressOf());
    m_Context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
    
    ID3D11SamplerState* samplers[] = { linearSampler.Get(), pointSampler.Get() };
    m_Context->CSSetSamplers(0, 2, samplers);

    m_Context->Dispatch((UINT)ceil(outDesc.Width / 16.0f), (UINT)ceil(outDesc.Height / 16.0f), 1);

    // Unbind
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    m_Context->CSSetShaderResources(0, 1, &nullSRV);
    m_Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    m_Context->CSSetSamplers(0, 0, nullptr);
}

#include <chrono>

void FrameGeneration::Capture(IDXGISwapChain* swapChain)
{
    auto start = std::chrono::high_resolution_clock::now();
    
	if (!m_Device || !m_Context) return;

	ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)))
		return;
    
    D3D11_TEXTURE2D_DESC desc;
	backBuffer->GetDesc(&desc);

	// Lazy initialization of storage texture
	if (!m_TexCurrent)
	{
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = 0;
		desc.CPUAccessFlags = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
        
        if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) 
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

		m_Device->CreateTexture2D(&desc, nullptr, &m_TexCurrent);
		m_Device->CreateTexture2D(&desc, nullptr, &m_TexPrev);

		D3D11_TEXTURE2D_DESC motionDesc = desc;
		motionDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		motionDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		m_Device->CreateTexture2D(&motionDesc, nullptr, &m_TexMotion);
		m_Device->CreateTexture2D(&desc, nullptr, &m_TexGenerated);

		Debug::Info("GPU Resource Pool initialized.");

		m_OpticalFlow.Initialize(m_Device.Get(), desc.Width, desc.Height);
		m_FrameInterpolation.Initialize(m_Device.Get(), desc.Width, desc.Height);
	}

    // Performance Mode Resources
    bool useScaling = (m_Settings.RenderScale < 1.0f);
    int targetW = (int)(desc.Width * m_Settings.RenderScale);
    int targetH = (int)(desc.Height * m_Settings.RenderScale);
    targetW = (targetW / 2) * 2; targetH = (targetH / 2) * 2; // Align
    if (targetW < 16) targetW = 16; if (targetH < 16) targetH = 16;

    if (useScaling)
    {
        D3D11_TEXTURE2D_DESC lrDesc = {};
        if (m_TexLowResCurrent) m_TexLowResCurrent->GetDesc(&lrDesc);
        
        if (!m_TexLowResCurrent || lrDesc.Width != targetW || lrDesc.Height != targetH)
        {
            D3D11_TEXTURE2D_DESC d = desc;
            d.Width = targetW; d.Height = targetH;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            m_Device->CreateTexture2D(&d, nullptr, &m_TexLowResCurrent);
            m_Device->CreateTexture2D(&d, nullptr, &m_TexLowResPrev);
            m_Device->CreateTexture2D(&d, nullptr, &m_TexLowResGenerated); 
            D3D11_TEXTURE2D_DESC md = d;
            md.Format = DXGI_FORMAT_R16G16_FLOAT;
            m_Device->CreateTexture2D(&md, nullptr, &m_TexLowResMotion);
            
            m_OpticalFlow.Initialize(m_Device.Get(), targetW, targetH);
        }
    }

	// [Cycle Frames]
	if (m_TexPrev && m_TexCurrent) m_TexPrev.Swap(m_TexCurrent);
    if (useScaling && m_TexLowResPrev && m_TexLowResCurrent) m_TexLowResPrev.Swap(m_TexLowResCurrent);

	// 2. Capture New Frame
	m_Context->CopyResource(m_TexCurrent.Get(), backBuffer.Get());
    
    // Downscale if needed
    if (useScaling)
    {
         DispatchScale(m_TexCurrent.Get(), m_TexLowResCurrent.Get());
    }

	// [Execute Pipeline]
	ID3D11DeviceContext* ctxToUse = (m_Settings.EnableAsyncCompute && m_DeferredContext) ? m_DeferredContext.Get() : m_Context.Get();

    // Select Resources
    ID3D11Texture2D* inputCurr = useScaling ? m_TexLowResCurrent.Get() : m_TexCurrent.Get();
    ID3D11Texture2D* inputPrev = useScaling ? m_TexLowResPrev.Get() : m_TexPrev.Get();
    ID3D11Texture2D* outputMotion = useScaling ? m_TexLowResMotion.Get() : m_TexMotion.Get();

	if (m_Settings.EnableBiDirFlow)
	{
		m_OpticalFlow.DispatchBiDirectional(ctxToUse, inputCurr, inputPrev, outputMotion,
			m_Settings.BlockSize, m_Settings.SearchRadius);
	}
	else if (m_Settings.EnableAdaptiveBlock)
	{
		m_OpticalFlow.DispatchAdaptive(ctxToUse, inputCurr, inputPrev, outputMotion,
			m_Settings.SearchRadius);
	}
	else
	{
		m_OpticalFlow.Dispatch(ctxToUse, inputCurr, inputPrev, outputMotion,
			m_Settings.BlockSize, m_Settings.SearchRadius,
			m_Settings.EnableSubPixel, m_Settings.EnableMotionSmoothing,
			m_Settings.MaxPyramidLevel, m_Settings.MinPyramidLevel,
			(FlowAlgorithm)m_Settings.OpticalFlowAlgorithm);	
	}

	// Execute Async Command List
	if (ctxToUse == m_DeferredContext.Get())
	{
		ComPtr<ID3D11CommandList> cmdList;
		m_DeferredContext->FinishCommandList(FALSE, &cmdList);
		m_Context->ExecuteCommandList(cmdList.Get(), FALSE);
	}

	// [Low Latency Mode]
	if (m_Settings.LowLatencyMode)
	{
		ComPtr<IDXGIDevice1> dxgiDevice;
		if (SUCCEEDED(m_Device.As(&dxgiDevice)))
		{
			dxgiDevice->SetMaximumFrameLatency(1);
		}
	}

	// 3. Frame Synthesis (Generate Intermediate Frame)
	if (m_Settings.DebugViewMode > 0)
	{
		// Render Debug View into m_TexGenerated (using current flow)
		m_FrameInterpolation.Dispatch(m_Context.Get(), 
			m_TexCurrent.Get(), 
			m_TexPrev.Get(), 
			m_TexMotion.Get(), 
			m_TexGenerated.Get(),
			m_OpticalFlow.GetStatsSRV(),
			m_Settings.HUDThreshold,
			m_Settings.DebugViewMode,
			m_Settings.MotionSensitivity,
			0.0f, // Factor doesn't matter for debug view usually
			m_Settings.SceneChangeThreshold,
			0.0f, 0.0f, false); // Disable RCAS/Ghosting/Edge for debug view
			
		// Overwrite the Real BackBuffer with the Debug View
		m_Context->CopyResource(backBuffer.Get(), m_TexGenerated.Get());
	}
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> duration = end - start;
    m_LastGenTime = duration.count();
}

bool FrameGeneration::PresentGenerated(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags, float factor)
{
	if (!m_TexGenerated || !m_Context) return false;

	// 3. Frame Synthesis (Generate Intermediate Frame)
	ID3D11DeviceContext* ctxToUse = (m_Settings.EnableAsyncCompute && m_DeferredContext) ? m_DeferredContext.Get() : m_Context.Get();

    bool useScaling = (m_Settings.RenderScale < 1.0f);
    ID3D11Texture2D* inputCurr = useScaling ? m_TexLowResCurrent.Get() : m_TexCurrent.Get();
    ID3D11Texture2D* inputPrev = useScaling ? m_TexLowResPrev.Get() : m_TexPrev.Get();
    ID3D11Texture2D* inputMotion = useScaling ? m_TexLowResMotion.Get() : m_TexMotion.Get();
    ID3D11Texture2D* outputGen = useScaling ? m_TexLowResGenerated.Get() : m_TexGenerated.Get();

	m_FrameInterpolation.Dispatch(ctxToUse, 
		inputCurr, 
		inputPrev, 
		inputMotion, 
		outputGen,
		m_OpticalFlow.GetStatsSRV(),
		m_Settings.HUDThreshold,
		m_Settings.DebugViewMode,
		m_Settings.MotionSensitivity,
		factor,
		m_Settings.SceneChangeThreshold,
		m_Settings.RcasStrength,
		m_Settings.GhostingReduction,
		m_Settings.EnableEdgeProtection);
        
    // [Upscale]
    if (useScaling && m_TexLowResGenerated)
    {
        // Upscale LowResGenerated -> TexGenerated (Native)
        DispatchScale(m_TexLowResGenerated.Get(), m_TexGenerated.Get());
    }

	// [Split Screen Comparison]
	if (m_Settings.EnableSplitScreen)
	{
		// Use m_TexSharpened as a temp scratch buffer to avoid Read/Write hazard on m_TexGenerated
		ID3D11Texture2D* pTemp = m_FrameInterpolation.GetTempTexture();
		if (pTemp)
		{
			// 1. Copy current Generated frame to Temp
			ctxToUse->CopyResource(pTemp, m_TexGenerated.Get());

			// 2. Dispatch Split Screen Shader
			// Input A (Left/Gen): Temp
			// Input B (Right/Real): m_TexPrev (The "No FG" experience)
			// Output: m_TexGenerated (Overwrite with Split View)
			m_FrameInterpolation.DispatchSplitScreen(ctxToUse, 
				pTemp, 
				m_TexPrev.Get(), 
				m_TexGenerated.Get(), 
				m_Settings.SplitScreenPosition);
		}
	}

	// Execute Async Command List
	if (ctxToUse == m_DeferredContext.Get())
	{
		ComPtr<ID3D11CommandList> cmdList;
		m_DeferredContext->FinishCommandList(FALSE, &cmdList);
		m_Context->ExecuteCommandList(cmdList.Get(), FALSE);
	}

	// 4. Inject
	// Copy Generated -> BackBuffer
	ID3D11Texture2D* pBackBuffer = nullptr;
	if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)))
		return false;

	// Copy Generated Frame to BackBuffer
	m_Context->CopyResource(pBackBuffer, m_TexGenerated.Get());
	return true; 
}

void FrameGeneration::RestoreOriginal(IDXGISwapChain* swapChain)
{
	if (!m_TexCurrent || !m_Context) return;

	ComPtr<ID3D11Texture2D> backBuffer;
	if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)))
		return;

	// [Debug Flicker Fix]
	// If Debug Mode is active, we want the "Original" frame (Real Frame) to ALSO have the overlay.
	// Since 'm_TexGenerated' was used for intermediate frames, it's dirty.
	// We must Regenerate the Debug View for the current frame.
	// [Split Screen Comparison] - Apply to Real Frame too for consistency (Line drawing)
	if (m_Settings.EnableSplitScreen)
	{
		// For the Real Frame:
		// Left (FG On)  = Frame N
		// Right (FG Off)= Frame N
		// So content is identical, but we need the LINE to be drawn so it doesn't flicker away.
		// We use m_TexSharpened as temp again.
		
		ID3D11Texture2D* pTemp = m_FrameInterpolation.GetTempTexture();
		if (pTemp)
		{
			// Copy Current -> Temp
			m_Context->CopyResource(pTemp, m_TexCurrent.Get());
			
			// Dispatch Split (Left=Temp, Right=Temp) -> Output to m_TexGenerated
			// We write to m_TexGenerated because we will copy THAT to backbuffer.
			m_FrameInterpolation.DispatchSplitScreen(m_Context.Get(), 
				pTemp, 
				pTemp, // Both sides are Frame N
				m_TexGenerated.Get(), 
				m_Settings.SplitScreenPosition);
				
			// Copy Result to BackBuffer
			m_Context->CopyResource(backBuffer.Get(), m_TexGenerated.Get());
			return; // Early exit as we handled the copy
		}
	}

	if (m_Settings.DebugViewMode > 0)
	{
		// ... existing Debug Logic ...
		m_FrameInterpolation.Dispatch(m_Context.Get(), 
			m_TexCurrent.Get(), 
			m_TexPrev.Get(), 
			m_TexMotion.Get(), 
			m_TexGenerated.Get(),
			m_OpticalFlow.GetStatsSRV(),
			m_Settings.HUDThreshold,
			m_Settings.DebugViewMode,
			m_Settings.MotionSensitivity,
			0.0f, // Factor 0.0
			m_Settings.SceneChangeThreshold,
			0.0f, 0.0f, false);

		m_Context->CopyResource(backBuffer.Get(), m_TexGenerated.Get());
	}
	else
	{
		// Restore Clean Original (Real Frame)
        // [Flicker Fix] Apply RCAS to the Real Frame too!
        bool applyRCAS = (m_Settings.RcasStrength > 0.0f);
        
        bool useScaling = (m_Settings.RenderScale < 0.99f);
        if (useScaling && m_TexLowResCurrent)
        {
            // Upscale: LowRes -> TexGenerated (UAV safe)
            DispatchScale(m_TexLowResCurrent.Get(), m_TexGenerated.Get());
            
            if (applyRCAS)
            {
                // Sharpen: TexGenerated -> Temp
                ID3D11Texture2D* pTemp = m_FrameInterpolation.GetTempTexture();
                if (pTemp)
                {
                    m_FrameInterpolation.DispatchRCAS(m_Context.Get(), 
                        m_TexGenerated.Get(), 
                        pTemp, 
                        m_Settings.RcasStrength);
                        
                    m_Context->CopyResource(backBuffer.Get(), pTemp);
                    return;
                }
            }
            
            // Backup: Copy Scaled to Backbuffer
            m_Context->CopyResource(backBuffer.Get(), m_TexGenerated.Get());
        }
        else
        {
            // Native
            if (applyRCAS)
            {
                // Unscaled but needs sharpening
                // Sharpen: Current -> TexGenerated (as temp)
                // Use TexGenerated as destination since we can't write UAV to BackBuffer
                m_FrameInterpolation.DispatchRCAS(m_Context.Get(), 
                    m_TexCurrent.Get(), 
                    m_TexGenerated.Get(), 
                    m_Settings.RcasStrength);
                    
                m_Context->CopyResource(backBuffer.Get(), m_TexGenerated.Get());
            }
            else
            {
                 // Pure Copy
                 m_Context->CopyResource(backBuffer.Get(), m_TexCurrent.Get());
            }
        }
	}
}

void FrameGeneration::Release()
{
	m_TexCurrent.Reset();
	m_Context.Reset();
	m_Device.Reset();
}
