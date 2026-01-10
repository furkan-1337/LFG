#include "Shader.h"
#include <Debug/Debug.h>

#pragma comment(lib, "d3dcompiler.lib")

bool Shader::CompileComputeShader(ID3D11Device* device, const std::wstring& filePath, const std::string& entryPoint, ID3D11ComputeShader** outShader)
{
	// Resolve full path relative to the DLL
	WCHAR modulePath[MAX_PATH];
	HMODULE hModule = nullptr;
	// Use the address of this function to get the HMODULE of the DLL
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Shader::CompileComputeShader, &hModule);
	GetModuleFileNameW(hModule, modulePath, MAX_PATH);
	
	std::wstring fullPath(modulePath);
	size_t lastSlash = fullPath.find_last_of(L"\\/");
	if (lastSlash != std::wstring::npos)
	{
		fullPath = fullPath.substr(0, lastSlash + 1); // Keep the trailing slash
	}
	fullPath += filePath;

	Debug::Info("Loading Shader from: %ls", fullPath.c_str());

	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> errorBlob;

	HRESULT hr = D3DCompileFromFile(
		fullPath.c_str(),
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entryPoint.c_str(),
		"cs_5_0",
		0,
		0,
		&blob,
		&errorBlob
	);

	if (FAILED(hr))
	{
		if (errorBlob)
		{
			Debug::Error("Shader Compile Error: %s", (char*)errorBlob->GetBufferPointer());
		}
		else
		{
			Debug::Error("Shader Compile Failed: HRESULT 0x%08X (Path: %ls)", hr, fullPath.c_str());
		}
		return false;
	}

	hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, outShader);
	if (FAILED(hr))
	{
		Debug::Error("Failed to create Compute Shader.");
		return false;
	}

	return true;
}

bool Shader::CompileComputeShaderFromMemory(ID3D11Device* device, const std::string& shaderSource, const std::string& entryPoint, ID3D11ComputeShader** outShader)
{
	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> errorBlob;

	HRESULT hr = D3DCompile(
		shaderSource.c_str(),
		shaderSource.length(),
		nullptr, // SourceName (optional)
		nullptr, // Defines
		D3D_COMPILE_STANDARD_FILE_INCLUDE, // Include Handler (Might fail if memory shader has includes, but our embedded shaders are flat)
		entryPoint.c_str(),
		"cs_5_0",
		0, // Flags1
		0, // Flags2
		&blob,
		&errorBlob
	);

	if (FAILED(hr))
	{
		if (errorBlob)
		{
			Debug::Error("Shader Memory Compile Error: %s", (char*)errorBlob->GetBufferPointer());
		}
		else
		{
			Debug::Error("Shader Memory Compile Failed: HRESULT 0x%08X", hr);
		}
		return false;
	}

	hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, outShader);
	if (FAILED(hr))
	{
		Debug::Error("Failed to create Compute Shader from memory.");
		return false;
	}

	return true;
}
