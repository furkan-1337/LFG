#pragma once
#include <d3d11.h>
#include <string>
#include <wrl/client.h>
#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;

class Shader
{
public:
	static bool CompileComputeShader(ID3D11Device* device, const std::wstring& filePath, const std::string& entryPoint, ID3D11ComputeShader** outShader);
	static bool CompileComputeShaderFromMemory(ID3D11Device* device, const std::string& shaderSource, const std::string& entryPoint, ID3D11ComputeShader** outShader);
};
