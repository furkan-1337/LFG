#include "EdgeDetection.h"
#include "../../Pipeline/Shaders/EmbeddedShaders.h"
#include "../Shaders/Shader.h"
#include <Debug/Debug.h>
#include <cmath>

using Microsoft::WRL::ComPtr;

// Helper functions (duplicated for isolation)
static void CreateSRV(ID3D11Device* dev, ID3D11Texture2D* tex, ID3D11ShaderResourceView** ppSRV) {
    if (!tex) return;
    dev->CreateShaderResourceView(tex, nullptr, ppSRV);
}

static void CreateUAV(ID3D11Device* dev, ID3D11Texture2D* tex, ID3D11UnorderedAccessView** ppUAV) {
    if (!tex) return;
    dev->CreateUnorderedAccessView(tex, nullptr, ppUAV);
}

bool EdgeDetection::Initialize(ID3D11Device* device, int width, int height)
{
    if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_EdgeDetect, "CSMain", &m_csEdgeDetect))
    {
        Debug::Error("Failed to load Edge Detect Shader");
        return false;
    }

    // Create Edge Texture (R8_UNORM)
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM; 
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    if (FAILED(device->CreateTexture2D(&desc, nullptr, &m_TexEdge))) 
    { 
        Debug::Error("Failed to create Edge Texture"); 
        return false;
    }

    return true;
}

void EdgeDetection::Dispatch(ID3D11DeviceContext* context, ID3D11Texture2D* input)
{
    if (!m_csEdgeDetect || !m_TexEdge) return;

    ID3D11Device* dev = nullptr;
    context->GetDevice(&dev);

    D3D11_TEXTURE2D_DESC desc;
    input->GetDesc(&desc);
    UINT groupsX = (UINT)ceil(desc.Width / 32.0f); // Edge Detect uses 32x32 threads
    UINT groupsY = (UINT)ceil(desc.Height / 32.0f);

    ComPtr<ID3D11ShaderResourceView> srvCurr;
    ComPtr<ID3D11UnorderedAccessView> uavEdge;
    
    CreateSRV(dev, input, &srvCurr);
    CreateUAV(dev, m_TexEdge.Get(), &uavEdge);

    context->CSSetShader(m_csEdgeDetect.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, srvCurr.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, uavEdge.GetAddressOf(), nullptr);
    
    context->Dispatch(groupsX, groupsY, 1);

    // Unbind
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);
    context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

    dev->Release();
}
