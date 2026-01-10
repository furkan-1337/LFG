#pragma once
#include <d3d11.h>
#include <wrl/client.h>



class EdgeDetection
{
public:
    EdgeDetection() = default;
    ~EdgeDetection() = default;

    bool Initialize(ID3D11Device* device, int width, int height);
    void Dispatch(ID3D11DeviceContext* context, ID3D11Texture2D* input);
    
    // Returns texture resource, caller needs to create SRV if needed or helper
    ID3D11Texture2D* GetOutputTexture() const { return m_TexEdge.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csEdgeDetect;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_TexEdge; // R8_UNORM
};
