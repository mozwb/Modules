#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace X_Y {

struct VertexAttrib {
    uint32_t Size;      //< byte size (Float3=12, Float4=16)
    uint32_t Offset;    //< byte offset within vertex
    std::string Name;   //< shader attribute name
};

struct MeshData {
    std::vector<float> Vertices;
    std::vector<uint32_t> Indices;
    uint32_t VertexStride = 0;          //< floats per vertex
    std::vector<VertexAttrib> Attribs;

    bool IsValid() const {
        return !Vertices.empty() && !Indices.empty() && VertexStride > 0;
    }

    void Clear() {
        Vertices.clear();
        Indices.clear();
        VertexStride = 0;
        Attribs.clear();
    }
};

} // namespace X_Y
