#pragma once
#include <DirectX/DirectX.h>
#include <Hook/Engine/HookEngine.h>
#include <unordered_map>

namespace Common
{
    inline uint150_t* GetMethodTable(D3DVersion version)
    {
        static std::unordered_map<D3DVersion, uint150_t*> tables;

        auto it = tables.find(version);
        if (it != tables.end())
            return it->second;

        auto table = DirectX::GetMethodTable(version);
        tables[version] = table;
        return table;
    }

    inline uint150_t* GetMethodByIndex(D3DVersion version, int index)
    {
        auto table = GetMethodTable(version);
        if (table)
            return reinterpret_cast<uint150_t*>(table[index]);
		return nullptr;
    }
}