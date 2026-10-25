#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include "Middle/MeshData.h"

namespace X_Y::Model {

// @brief 三角面顶点索引（1-based OBJ → 0-based 存储）
struct Index {
    int vertex   = -1;  // v
    int texcoord = -1;  // vt
    int normal   = -1;  // vn
};

// @brief 单个 Mesh
struct Mesh {
    std::string name;
    std::vector<Index> indices;
};

// @brief 平铺属性数组
struct Model {
    std::vector<float> vertices;   // 每 3 个一组 (x, y, z)
    std::vector<float> normals;    // 每 3 个一组 (nx, ny, nz)
    std::vector<float> texcoords;  // 每 2 个一组 (u, v)
    std::vector<Mesh> meshes;

    static constexpr uint32_t kDefaultStride = 6;

    bool ConvertMeshToData(size_t meshIndex, X_Y::MeshData& out, std::string& err) const
    {
        out.Clear();

        if (meshIndex >= meshes.size())
        {
            err = "Mesh index out of range";
            return false;
        }

        const auto& mesh = meshes[meshIndex];
        if (mesh.indices.empty())
        {
            err = "Mesh has no indices";
            return false;
        }

        const size_t vert_count = vertices.size() / 3;
        const size_t norm_count = normals.size() / 3;

        if (vert_count == 0)
        {
            err = "Model has no vertex data";
            return false;
        }

        out.VertexStride = kDefaultStride;
        out.Attribs = {
            { 12, 0,  "a_Position" },
            { 12, 12, "a_Normal"   }
        };
        out.Vertices.reserve(mesh.indices.size() * kDefaultStride);
        out.Indices.reserve(mesh.indices.size());

        // 顶点去重
        struct Key { int v, vt, vn; };
        auto keyHash = [](const Key& k) {
            return static_cast<size_t>(k.v + 1) ^
                   (static_cast<size_t>(k.vt + 1) << 7) ^
                   (static_cast<size_t>(k.vn + 1) << 15);
        };
        auto keyEq = [](const Key& a, const Key& b) {
            return a.v == b.v && a.vt == b.vt && a.vn == b.vn;
        };
        std::unordered_map<Key, uint32_t, decltype(keyHash), decltype(keyEq)> unique_map(0, keyHash, keyEq);
        unique_map.reserve(mesh.indices.size());

        for (const auto& idx : mesh.indices)
        {
            Key key{ idx.vertex, idx.texcoord, idx.normal };
            auto it = unique_map.find(key);
            if (it != unique_map.end())
            {
                out.Indices.push_back(it->second);
                continue;
            }

            uint32_t new_index = static_cast<uint32_t>(out.Vertices.size() / kDefaultStride);

            // Position
            if (idx.vertex < 0 || static_cast<size_t>(idx.vertex) >= vert_count)
            {
                err = "Vertex index out of range";
                return false;
            }

            size_t v_off = static_cast<size_t>(idx.vertex) * 3;
            out.Vertices.push_back(vertices[v_off + 0]);
            out.Vertices.push_back(vertices[v_off + 1]);
            out.Vertices.push_back(vertices[v_off + 2]);

            // Normal
            if (idx.normal >= 0 && static_cast<size_t>(idx.normal) < norm_count)
            {
                size_t n_off = static_cast<size_t>(idx.normal) * 3;
                out.Vertices.push_back(normals[n_off + 0]);
                out.Vertices.push_back(normals[n_off + 1]);
                out.Vertices.push_back(normals[n_off + 2]);
            }
            else
            {
                out.Vertices.push_back(0.0f);
                out.Vertices.push_back(0.0f);
                out.Vertices.push_back(0.0f);
            }

            unique_map[key] = new_index;
            out.Indices.push_back(new_index);
        }

        return true;
    }

    void Clear() {
        vertices.clear();
        normals.clear();
        texcoords.clear();
        meshes.clear();
    }
};

// @brief 从文件加载 .obj
bool LoadObj(const std::string& filepath, Model& out_model, std::string& err);

// @brief 从内存数据加载 .obj
bool LoadObjFromMemory(const char* data, size_t size, Model& out_model, std::string& err);

}
