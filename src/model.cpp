#include "model.h"
#include "pak.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

// tinygltf needs its image decoder compiled exactly once, so it's enabled here.
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

// Reads one index of any unsigned integer type
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

GLuint makeTexture(int w, int h, GLenum internalFmt, GLenum fmt, const unsigned char* pixels) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);
    return t;
}

} // namespace

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

    // Embedded images -> GL textures
    std::vector<GLuint> imageTex(gltf.images.size(), 0);
    for (size_t i = 0; i < gltf.images.size(); ++i) {
        const tinygltf::Image& img = gltf.images[i];
        if (img.image.empty() || (img.component != 3 && img.component != 4)) continue;
        GLenum fmt = img.component == 4 ? GL_RGBA : GL_RGB;
        imageTex[i] = makeTexture(img.width, img.height, fmt, fmt, img.image.data());
        m.textures.push_back(imageTex[i]);
    }

    // 1x1 texture for primitives with a plain color instead of a texture
    auto solid = [&](unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
        unsigned char px[4] = { r, g, b, a };
        GLuint t = makeTexture(1, 1, GL_RGBA, GL_RGBA, px);
        m.textures.push_back(t);
        return t;
    };
    auto toByte = [](double v) { return (unsigned char)(std::max(0.0, std::min(1.0, v)) * 255.0); };

    std::vector<float> verts;     // 5 floats per vertex
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

            // The base color texture if there is one, otherwise the material's color
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
    // Positions and triangles kept for picking
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
    return true;
}

void destroyModel(Model& m) {
    for (GLuint t : m.textures) {
        if (t) glDeleteTextures(1, &t);
    }
    if (m.ebo) glDeleteBuffers(1, &m.ebo);
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    m = Model{};
}
