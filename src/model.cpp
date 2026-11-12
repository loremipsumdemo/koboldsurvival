#include "model.h"
#include "pak.h"
#include "gl_util.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

namespace {
struct Accessor {
    const unsigned char* data = nullptr;
    size_t count = 0;
    size_t stride = 0;
    int compType = 0;
};

bool getAccessor(const tinygltf::Model& g, int index, Accessor& out) {
    if (index < 0 || index >= (int)g.accessors.size()) return false;
    const tinygltf::Accessor& acc = g.accessors[index];
    if (acc.bufferView < 0 || acc.bufferView >= (int)g.bufferViews.size()) return false;
    const tinygltf::BufferView& bv = g.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)g.buffers.size()) return false;
    const tinygltf::Buffer& buf = g.buffers[bv.buffer];

    int comps = tinygltf::GetNumComponentsInType(acc.type);
    int bytes = tinygltf::GetComponentSizeInBytes(acc.componentType);
    out.data = buf.data.data() + bv.byteOffset + acc.byteOffset;
    out.count = acc.count;
    out.stride = bv.byteStride ? bv.byteStride : (size_t)(comps * bytes);
    out.compType = acc.componentType;
    return true;
}

GLuint readIndex(const Accessor& a, size_t i) {
    const unsigned char* p = a.data + i * a.stride;
    switch (a.compType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return *p;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    default: {
        uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    }
}
}

namespace {
struct LodMesh {
    std::vector<float> verts;   // 5 floats a vertex, as the model's own
    std::vector<GLuint> indices;
    std::vector<DrawRange> draws;
};

// The mesh with its vertices merged by a grid of cells of this size. Two things keep its shape and its picture:
//  - a merged vertex is put at the average place of EVERYTHING in its cell, whichever texture patch or draw range the original
//    came from, so vertices that sat together on a seam still sit together and no crack opens between them;
//  - vertices are only merged if they lie in the same coarse patch (1/4 by 1/4) of their texture, and a merged vertex takes the
//    average texture position of those it merges, so the picture is not smeared across seams.
// A triangle left with fewer than three different corners is dropped. Each draw range (one texture) keeps its own triangles.
LodMesh clusterMesh(const std::vector<float>& verts, const std::vector<GLuint>& indices, const std::vector<DrawRange>& draws,
                    float cell, const float lo[3]) {
    struct Sum { float x = 0, y = 0, z = 0; int n = 0; };
    auto cellOf = [&](float x, float low) {
        return (std::uint64_t)std::min(65535.0f, std::max(0.0f, std::floor((x - low) / cell)));
    };
    auto placeKey = [&](const float* p) { return cellOf(p[0], lo[0]) | cellOf(p[1], lo[1]) << 16 | cellOf(p[2], lo[2]) << 32; };
    // Where each cell's vertices average to, over every vertex any triangle uses
    std::unordered_map<std::uint64_t, Sum> places;
    std::vector<char> seen(verts.size() / 5, 0);
    for (const GLuint v : indices) {
        if (seen[v]) continue;
        seen[v] = 1;
        const float* p = &verts[(size_t)v * 5];
        Sum& s = places[placeKey(p)];
        s.x += p[0];
        s.y += p[1];
        s.z += p[2];
        ++s.n;
    }
    LodMesh out;
    for (const DrawRange& d : draws) {
        struct Made { GLuint index = 0; float u = 0, v = 0; int n = 0; };
        std::unordered_map<std::uint64_t, Made> groups;   // by place cell and texture patch, in this draw range only
        std::vector<GLuint> mapped((size_t)d.indexCount);
        std::unordered_map<GLuint, GLuint> remap;         // original vertex to merged one
        for (GLsizei i = 0; i < d.indexCount; ++i) {
            const GLuint v = indices[(size_t)(d.firstIndex + i)];
            auto known = remap.find(v);
            if (known == remap.end()) {
                const float* p = &verts[(size_t)v * 5];
                const std::uint64_t patch = ((std::uint64_t)((int)std::floor(p[3] * 4.0f) & 0xFF) << 48) |
                                            ((std::uint64_t)((int)std::floor(p[4] * 4.0f) & 0xFF) << 56);
                Made& g = groups[placeKey(p) | patch];
                if (g.n == 0) {
                    const Sum& s = places[placeKey(p)];
                    g.index = (GLuint)(out.verts.size() / 5);
                    out.verts.insert(out.verts.end(), { s.x / (float)s.n, s.y / (float)s.n, s.z / (float)s.n, 0.0f, 0.0f });
                }
                g.u += p[3];
                g.v += p[4];
                ++g.n;
                known = remap.emplace(v, g.index).first;
            }
            mapped[(size_t)i] = known->second;
        }
        for (const auto& kv : groups) {
            float* uv = &out.verts[(size_t)kv.second.index * 5 + 3];
            uv[0] = kv.second.u / (float)kv.second.n;
            uv[1] = kv.second.v / (float)kv.second.n;
        }
        DrawRange made{ d.texture, 0, (GLsizei)out.indices.size() };
        for (GLsizei i = 0; i + 2 < d.indexCount; i += 3) {
            const GLuint a = mapped[(size_t)i], b = mapped[(size_t)i + 1], c = mapped[(size_t)i + 2];
            if (a == b || b == c || a == c) continue;
            out.indices.insert(out.indices.end(), { a, b, c });
        }
        made.indexCount = (GLsizei)out.indices.size() - made.firstIndex;
        if (made.indexCount > 0) out.draws.push_back(made);
    }
    return out;
}

// The coarse copy with about this many triangles, by trying cell sizes (the count falls roughly with the square of the size)
bool makeLod(const std::vector<float>& verts, const std::vector<GLuint>& indices, const std::vector<DrawRange>& draws,
             const float lo[3], float diagonal, size_t target, LodMesh& out) {
    float cell = diagonal / std::sqrt((float)target) * 0.5f;
    out = clusterMesh(verts, indices, draws, cell, lo);
    for (int pass = 0; pass < 3; ++pass) {
        const float n = (float)(out.indices.size() / 3);
        if (n > (float)target * 0.8f && n < (float)target * 1.25f) break;
        cell *= std::sqrt(std::max(n, 1.0f) / (float)target);
        out = clusterMesh(verts, indices, draws, cell, lo);
    }
    return !out.indices.empty() && out.indices.size() * 10 < indices.size() * 9;
}

void setLodLayout(GLuint vao, GLuint vbo, GLuint ebo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}
}

bool loadGLBModel(const char* path, Model& m) {
    tinygltf::Model gltf;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind(path, bytes, size)) {
        SDL_Log("Not in data.pak: %s", path);
        return false;
    }
    if (!loader.LoadBinaryFromMemory(&gltf, &err, &warn, bytes, (unsigned int)size)) {
        SDL_Log("Could not load %s: %s", path, err.c_str());
        return false;
    }

    std::vector<GLuint> imageTex(gltf.images.size(), 0);
    for (size_t i = 0; i < gltf.images.size(); ++i) {
        const tinygltf::Image& img = gltf.images[i];
        if (img.image.empty() || (img.component != 3 && img.component != 4)) continue;
        GLenum fmt = img.component == 4 ? GL_RGBA : GL_RGB;
        imageTex[i] = makeTexture2D(img.width, img.height, fmt, fmt, img.image.data(), GL_LINEAR, GL_REPEAT);
        m.textures.push_back(imageTex[i]);
    }

    auto solid = [&](unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
        unsigned char px[4] = { r, g, b, a };
        GLuint t = makeTexture2D(1, 1, GL_RGBA, GL_RGBA, px, GL_LINEAR, GL_REPEAT);
        m.textures.push_back(t);
        return t;
    };
    auto toByte = [](double v) { return (unsigned char)(std::max(0.0, std::min(1.0, v)) * 255.0); };

    std::vector<float> verts;
    std::vector<GLuint> indices;
    const float inf = std::numeric_limits<float>::infinity();
    float minX = inf, minY = inf, minZ = inf, maxX = -inf, maxY = -inf, maxZ = -inf;

    for (const tinygltf::Mesh& mesh : gltf.meshes) {
        for (const tinygltf::Primitive& prim : mesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;
            Accessor pos;
            if (!getAccessor(gltf, posIt->second, pos) || pos.compType != TINYGLTF_COMPONENT_TYPE_FLOAT) continue;

            Accessor uv;
            bool hasUV = false;
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end() && getAccessor(gltf, uvIt->second, uv)
                && uv.compType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                hasUV = true;
            }

            GLuint base = (GLuint)(verts.size() / 5);
            for (size_t v = 0; v < pos.count; ++v) {
                float p[3];
                std::memcpy(p, pos.data + v * pos.stride, sizeof(p));
                float t[2] = { 0.0f, 0.0f };
                if (hasUV) std::memcpy(t, uv.data + v * uv.stride, sizeof(t));

                verts.insert(verts.end(), { p[0], p[1], p[2], t[0], t[1] });
                minX = std::min(minX, p[0]); maxX = std::max(maxX, p[0]);
                minY = std::min(minY, p[1]); maxY = std::max(maxY, p[1]);
                minZ = std::min(minZ, p[2]); maxZ = std::max(maxZ, p[2]);
            }

            GLsizei firstIndex = (GLsizei)indices.size();
            Accessor idx;
            if (prim.indices >= 0 && getAccessor(gltf, prim.indices, idx)) {
                for (size_t i = 0; i < idx.count; ++i) indices.push_back(base + readIndex(idx, i));
            } else {
                for (size_t v = 0; v < pos.count; ++v) indices.push_back(base + (GLuint)v);
            }
            GLsizei count = (GLsizei)indices.size() - firstIndex;

            GLuint tex = 0;
            if (prim.material >= 0 && prim.material < (int)gltf.materials.size()) {
                const tinygltf::Material& mat = gltf.materials[prim.material];
                int ti = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (ti >= 0 && ti < (int)gltf.textures.size()) {
                    int src = gltf.textures[ti].source;
                    if (src >= 0 && src < (int)imageTex.size()) tex = imageTex[src];
                }
                if (!tex) {
                    const std::vector<double>& f = mat.pbrMetallicRoughness.baseColorFactor;
                    if (f.size() == 4) {
                        tex = solid(toByte(f[0]), toByte(f[1]), toByte(f[2]), toByte(f[3]));
                    }
                }
            }
            if (!tex) tex = solid(255, 255, 255, 255);

            m.draws.push_back({ tex, count, firstIndex });
        }
    }

    if (verts.empty() || indices.empty()) {
        SDL_Log("%s has no triangle geometry", path);
        destroyModel(m);
        return false;
    }

    m.minX = minX; m.minY = minY; m.minZ = minZ;
    m.maxX = maxX; m.maxY = maxY; m.maxZ = maxZ;
    m.positions.reserve(verts.size() / 5 * 3);
    for (size_t i = 0; i + 4 < verts.size(); i += 5) m.positions.insert(m.positions.end(), { verts[i], verts[i + 1], verts[i + 2] });
    m.triangles = indices;

    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // A model with this many triangles is too much to draw whole for a unit far off: one coarser copy, a quarter of the
    // triangles, for that
    constexpr size_t LOD_FROM_TRIANGLES = 4000;
    const size_t triangleCount = indices.size() / 3;
    if (triangleCount > LOD_FROM_TRIANGLES) {
        const float lo[3] = { minX, minY, minZ };
        const float diagonal = std::sqrt((maxX - minX) * (maxX - minX) + (maxY - minY) * (maxY - minY) + (maxZ - minZ) * (maxZ - minZ));
        LodMesh mesh;
        if (makeLod(verts, indices, m.draws, lo, diagonal, triangleCount / 4, mesh)) {
            Model lod;
            lod.minX = minX; lod.minY = minY; lod.minZ = minZ;
            lod.maxX = maxX; lod.maxY = maxY; lod.maxZ = maxZ;
            lod.draws = mesh.draws;
            lod.triangles = mesh.indices;
            glGenVertexArrays(1, &lod.vao);
            glGenBuffers(1, &lod.vbo);
            glGenBuffers(1, &lod.ebo);
            glBindBuffer(GL_ARRAY_BUFFER, lod.vbo);
            glBufferData(GL_ARRAY_BUFFER, mesh.verts.size() * sizeof(float), mesh.verts.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lod.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(GLuint), mesh.indices.data(), GL_STATIC_DRAW);
            setLodLayout(lod.vao, lod.vbo, lod.ebo);
            m.lods.push_back(std::move(lod));
        }
    }
    return true;
}

void destroyModel(Model& m) {
    for (Model& lod : m.lods) {   // their textures are this model's; the buffers are their own
        if (lod.ebo) glDeleteBuffers(1, &lod.ebo);
        if (lod.vbo) glDeleteBuffers(1, &lod.vbo);
        if (lod.vao) glDeleteVertexArrays(1, &lod.vao);
        if (lod.instVao) glDeleteVertexArrays(1, &lod.instVao);
    }
    for (GLuint t : m.textures) {
        if (t) glDeleteTextures(1, &t);
    }
    if (m.ebo) glDeleteBuffers(1, &m.ebo);
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    if (m.instVao) glDeleteVertexArrays(1, &m.instVao);
    m = Model{};
}
