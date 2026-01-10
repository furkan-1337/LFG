#pragma once
#include <d3d11.h>
#include <wrl/client.h>



class Sharpening
{
public:
    Sharpening() = default;
    ~Sharpening() = default;

    bool Initialize(ID3D11Device* device);
    void Dispatch(ID3D11DeviceContext* context, ID3D11Texture2D* input, ID3D11Texture2D* output, float strength);

private:
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csRCAS;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbRCAS;

    struct CBRCAS {
        float Sharpness;
        float Padding[3];
    };
};
