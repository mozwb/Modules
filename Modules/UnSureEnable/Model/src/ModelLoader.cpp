#include "Model/ModelLoader.h"
#include "Files/Files.h"
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

namespace X_Y::Model
{

    // ----------------------------------------------------------------
    // 逐行解析 OBJ（用 sscanf / stringstream，不靠指针跳转）
    // ----------------------------------------------------------------

    static std::vector<std::string> split(const std::string &s, char delim)
    {
        std::vector<std::string> parts;
        std::istringstream iss(s);
        std::string token;
        while (std::getline(iss, token, delim))
        {
            if (!token.empty())
                parts.push_back(token);
        }
        return parts;
    }

    // 将 OBJ 1-based 索引转为 0-based。
    // objIdx <= 0 (未设置/无效) 时返回 -1；
    // objIdx > 0 时返回 objIdx - 1（1-based → 0-based）；
    // objIdx < 0 时（相对索引）返回 count + objIdx。
    static int resolveIdx(int objIdx, int count)
    {
        if (objIdx > 0)
            return objIdx - 1;
        if (objIdx == 0 || objIdx == -1)
            return -1; // OBJ 里 0 非法，-1 表示未设置
        if (objIdx < 0)
            return count + objIdx; // 相对索引
        return -1;
    }

    bool LoadObjFromMemory(const char *data, size_t size, Model &out_model, std::string &err)
    {
        out_model.Clear();

        std::string content(data, size);
        std::istringstream stream(content);
        std::string line;

        std::vector<int> face_v, face_vt, face_vn;
        Mesh current_mesh;
        bool has_mesh = false;

        auto flushMesh = [&]()
        {
            if (has_mesh && !current_mesh.indices.empty())
            {
                if (current_mesh.name.empty())
                    current_mesh.name = "default";
                out_model.meshes.push_back(std::move(current_mesh));
            }
            current_mesh = Mesh();
            has_mesh = false;
        };

        while (std::getline(stream, line))
        {
            // 去除首尾空白
            auto trimStart = line.find_first_not_of(" \t\r");
            if (trimStart == std::string::npos)
                continue;
            line = line.substr(trimStart);

            if (line.empty() || line[0] == '#')
                continue;

            // 按空格分割第一个 token
            auto firstSpace = line.find_first_of(" \t");
            std::string type = (firstSpace == std::string::npos) ? line : line.substr(0, firstSpace);
            std::string rest = (firstSpace == std::string::npos) ? "" : line.substr(firstSpace + 1);

            if (type == "v")
            {
                float x, y, z;
                if (sscanf_s(rest.c_str(), "%f %f %f", &x, &y, &z) >= 3)
                {
                    out_model.vertices.push_back(x);
                    out_model.vertices.push_back(y);
                    out_model.vertices.push_back(z);
                }
            }
            else if (type == "vn")
            {
                float nx, ny, nz;
                if (sscanf_s(rest.c_str(), "%f %f %f", &nx, &ny, &nz) >= 3)
                {
                    out_model.normals.push_back(nx);
                    out_model.normals.push_back(ny);
                    out_model.normals.push_back(nz);
                }
            }
            else if (type == "vt")
            {
                float u, v;
                if (sscanf_s(rest.c_str(), "%f %f", &u, &v) >= 2)
                {
                    out_model.texcoords.push_back(u);
                    out_model.texcoords.push_back(v);
                }
            }
            else if (type == "f")
            {
                if (!has_mesh)
                    has_mesh = true;

                std::vector<std::string> tokens = split(rest, ' ');
                face_v.clear();
                face_vt.clear();
                face_vn.clear();

                for (const auto &tok : tokens)
                {
                    int vi = 0, ti = -1, ni = -1;

                    // 先尝试 v//vn (格式: 5//1)
                    if (sscanf_s(tok.c_str(), "%d//%d", &vi, &ni) >= 2)
                    {
                        // 成功，vi 和 ni 已设置，ti 保持 -1
                    }
                    // 再尝试 v/vt/vn, v/vt, v (格式: 5/1/1, 5/1, 5)
                    else if (sscanf_s(tok.c_str(), "%d/%d/%d", &vi, &ti, &ni) >= 1)
                    {
                        // 至少 vi 已设置
                    }
                    else
                    {
                        continue; // 所有格式都解析失败，跳过该 token
                    }

                    int vertCount = (int)(out_model.vertices.size() / 3);
                    int texCount = (int)(out_model.texcoords.size() / 2);
                    int normCount = (int)(out_model.normals.size() / 3);

                    face_v.push_back(resolveIdx(vi, vertCount));
                    face_vt.push_back(resolveIdx(ti, texCount));
                    face_vn.push_back(resolveIdx(ni, normCount));
                }

                // triangulate
                size_t n = face_v.size();
                if (n >= 3)
                {
                    for (size_t i = 1; i + 1 < n; ++i)
                    {
                        current_mesh.indices.push_back({face_v[0], face_vt[0], face_vn[0]});
                        current_mesh.indices.push_back({face_v[i], face_vt[i], face_vn[i]});
                        current_mesh.indices.push_back({face_v[i + 1], face_vt[i + 1], face_vn[i + 1]});
                    }
                }
            }
            else if (type == "g" || type == "o")
            {
                if (has_mesh && !current_mesh.indices.empty())
                {
                    out_model.meshes.push_back(std::move(current_mesh));
                    current_mesh = Mesh();
                }

                // 提取 name
                auto nameStart = rest.find_first_not_of(" \t");
                if (nameStart != std::string::npos)
                {
                    auto nameEnd = rest.find_first_of(" \t", nameStart);
                    current_mesh.name = rest.substr(nameStart, nameEnd - nameStart);
                }
                has_mesh = true;
            }
            else if (type == "usemtl")
            {
                if (has_mesh && !current_mesh.indices.empty())
                {
                    out_model.meshes.push_back(std::move(current_mesh));
                    current_mesh = Mesh();
                }
                // 材质名作为 mesh 名
                auto nameStart = rest.find_first_not_of(" \t");
                if (nameStart != std::string::npos)
                    current_mesh.name = rest.substr(nameStart);
                has_mesh = true;
            }
            else
            {
                // mtllib, s 等跳过
            }
        }

        // flush last
        if (has_mesh && !current_mesh.indices.empty())
        {
            if (current_mesh.name.empty())
                current_mesh.name = "default";
            out_model.meshes.push_back(std::move(current_mesh));
        }

        // 如果没有任何 mesh 但有顶点，创建默认 mesh
        if (out_model.meshes.empty() && !out_model.vertices.empty())
        {
            Mesh def;
            def.name = "default";
            out_model.meshes.push_back(std::move(def));
        }

        return true;
    }

    bool LoadObj(const std::string &filepath, Model &out_model, std::string &err)
    {
        Buffer buf = FilesSystem::ReadFileBinary(filepath);
        if (!buf.Size)
        {
            err = "Failed to read file: " + filepath;
            return false;
        }

        return LoadObjFromMemory(
            reinterpret_cast<const char *>(buf.Data),
            buf.Size,
            out_model,
            err);
    }

}
// namespace X_Y::Model
