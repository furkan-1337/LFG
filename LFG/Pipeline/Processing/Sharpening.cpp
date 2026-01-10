#include "Sharpening.h"
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

bool Sharpening::Initialize(ID3D11Device* device)
{
    if (!Shader::CompileComputeShaderFromMemory(device, EmbeddedShaders::CS_RCAS, "CSMain", &m_csRCAS))
    {
        Debug::Error("Failed to load RCAS Shader");
        return false;
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.ByteWidth = sizeof(CBRCAS);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_cbRCAS)))
    {
        Debug::Error("Failed to create RCAS Constant Buffer");
        return false;
    }

    return true;
}

void Sharpening::Dispatch(ID3D11DeviceContext* context, ID3D11Texture2D* input, ID3D11Texture2D* output, float strength)
{
    if (!m_csRCAS || strength <= 0.001f) return;

    ID3D11Device* dev = nullptr;
    context->GetDevice(&dev);

    D3D11_TEXTURE2D_DESC desc;
    input->GetDesc(&desc);
    UINT groupsX = (UINT)ceil(desc.Width / 8.0f);
    UINT groupsY = (UINT)ceil(desc.Height / 8.0f);

    // Update CB
    CBRCAS cbData = { strength, {0,0,0} };
    context->UpdateSubresource(m_cbRCAS.Get(), 0, nullptr, &cbData, 0, 0);

    ComPtr<ID3D11ShaderResourceView> srvInput;
    ComPtr<ID3D11UnorderedAccessView> uavOutput;

    CreateSRV(dev, input, &srvInput);
    CreateUAV(dev, output, &uavOutput);

    context->CSSetShader(m_csRCAS.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, m_cbRCAS.GetAddressOf());
    context->CSSetShaderResources(0, 1, srvInput.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, uavOutput.GetAddressOf(), nullptr);

    context->Dispatch(groupsX, groupsY, 1);

    // Unbind
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);
    context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

    dev->Release();
}
