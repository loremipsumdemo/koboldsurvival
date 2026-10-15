#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <GL/glew.h>
#include "tiny_gltf.h"
#include "model.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>

using Mat4d = std::array<double, 16>;  // column-major, matching the rest of the project

static Mat4d mulD(const Mat4d& a, const Mat4d& b) {
    Mat4d r{};
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += a[k * 4 + rw] * b[c * 4 + k];
            r[c * 4 + rw] = s;
        }
    return r;
}

// Local transform of a glTF node: either its matrix, or translation/rotation/scale
static Mat4d nodeLocal(const tinygltf::Node& n) {
    if (n.matrix.size() == 16) {
        Mat4d r{};
        for (int i = 0; i < 16; i++) r[i] = n.matrix[i];
        return r;
    }

    double t[3] = { 0, 0, 0 }, s[3] = { 1, 1, 1 }, q[4] = { 0, 0, 0, 1 };
    if (n.translation.size() == 3) for (int i = 0; i < 3; i++) t[i] = n.translation[i];
    if (n.scale.size() == 3)       for (int i = 0; i < 3; i++) s[i] = n.scale[i];
    if (n.rotation.size() == 4)    for (int i = 0; i < 4; i++) q[i] = n.rotation[i];

    double x = q[0], y = q[1], z = q[2], w = q[3];
    double r[9] = {  // rotation matrix from quaternion, column-major
        1 - 2 * (y * y + z * z), 2 * (x * y + w * z),     2 * (x * z - w * y),
        2 * (x * y - w * z),     1 - 2 * (x * x + z * z), 2 * (y * z + w * x),
        2 * (x * z + w * y),     2 * (y * z - w * x),     1 - 2 * (x * x + y * y)
    };

    Mat4d m{};
    for (int c = 0; c < 3; c++)
        for (int rw = 0; rw < 3; rw++)
            m[c * 4 + rw] = r[c * 3 + rw] * s[c];   // apply scale per column
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
    return m;
}

static const unsigned char* accessorPtr(const tinygltf::Model& m,
    const tinygltf::Accessor& acc, size_t& stride) {
    const auto& bv = m.bufferViews[acc.bufferView];
    const auto& buf = m.buffers[bv.buffer];
    int comps = tinygltf::GetNumComponentsInType(acc.type);
    int csize = tinygltf::GetComponentSizeInBytes(acc.componentType);
    stride = bv.byteStride ? bv.byteStride : (size_t)(comps * csize);
    return buf.data.data() + bv.byteOffset + acc.byteOffset;
}

// Reads float attributes (positions, UVs). Returns false if the format is not float.
static bool readFloats(const tinygltf::Model& m, int accIdx, int comps, std::vector<float>& out) {
    const auto& acc = m.accessors[accIdx];
    if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return false;

    size_t stride;
    const unsigned char* p = accessorPtr(m, acc, stride);
    out.resize(acc.count * comps);
    for (size_t i = 0; i < acc.count; i++)
        memcpy(&out[i * comps], p + i * stride, comps * sizeof(float));
    return true;
}

static void readIndices(const tinygltf::Model& m, int accIdx, std::vector<uint32_t>& out) {
    const auto& acc = m.accessors[accIdx];
    size_t stride;
    const unsigned char* p = accessorPtr(m, acc, stride);
    out.resize(acc.count);
    for (size_t i = 0; i < acc.count; i++) {
        const unsigned char* e = p + i * stride;
        switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            out[i] = *e;
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t v; memcpy(&v, e, 2); out[i] = v;
            break;
        }
        default: {
            uint32_t v; memcpy(&v, e, 4); out[i] = v;
            break;
        }
        }
    }
}

static GLuint makeTexture(int w, int h, int comps, const unsigned char* px) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLenum fmt = (comps == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}

static GLuint makeSolid(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    unsigned char px[4] = { r, g, b, a };
    return makeTexture(1, 1, 4, px);
}

bool loadGLBModel(const char* path, Model& out) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model gltf;
    std::string err, warn;

    if (!loader.LoadBinaryFromFile(&gltf, &err, &warn, path)) {
        SDL_Log("Failed to load %s: %s", path, err.c_str());
        return false;
    }
    if (!warn.empty()) SDL_Log("glTF warning: %s", warn.c_str());

    std::vector<float>    verts;     // interleaved: x y z u v
    std::vector<uint32_t> indices;
    std::map<int, GLuint> texCache;  // material index -> GL texture

    out.minX = out.minY = out.minZ = 1e30f;
    out.maxX = out.maxY = out.maxZ = -1e30f;

    auto getTexture = [&](int matIdx) -> GLuint {
        auto it = texCache.find(matIdx);
        if (it != texCache.end()) return it->second;

        GLuint tex = 0;
        if (matIdx >= 0) {
            const auto& pbr = gltf.materials[matIdx].pbrMetallicRoughness;
            int ti = pbr.baseColorTexture.index;
            if (ti >= 0 && gltf.textures[ti].source >= 0) {
                const auto& img = gltf.images[gltf.textures[ti].source];
                if (img.component == 3 || img.component == 4)
                    tex = makeTexture(img.width, img.height, img.component, img.image.data());
            }
            if (!tex) {  // untextured material: use its base colour factor
                auto toByte = [](double v) {
                    return (unsigned char)std::clamp(v * 255.0, 0.0, 255.0);
                    };
                const auto& f = pbr.baseColorFactor;
                tex = makeSolid(toByte(f[0]), toByte(f[1]), toByte(f[2]), toByte(f[3]));
            }
        }
        if (!tex) tex = makeSolid(255, 255, 255, 255);

        texCache[matIdx] = tex;
        return tex;
        };

    std::function<void(int, const Mat4d&)> visit = [&](int ni, const Mat4d& parent) {
        const auto& node = gltf.nodes[ni];
        Mat4d world = mulD(parent, nodeLocal(node));

        if (node.mesh >= 0) {
            for (const auto& prim : gltf.meshes[node.mesh].primitives) {
                if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

                auto pit = prim.attributes.find("POSITION");
                if (pit == prim.attributes.end()) continue;

                std::vector<float> pos, uv;
                readFloats(gltf, pit->second, 3, pos);
                size_t vcount = pos.size() / 3;

                auto tit = prim.attributes.find("TEXCOORD_0");
                bool hasUV = tit != prim.attributes.end() && readFloats(gltf, tit->second, 2, uv);
                if (!hasUV) uv.assign(vcount * 2, 0.0f);

                uint32_t base = (uint32_t)(verts.size() / 5);
                for (size_t i = 0; i < vcount; i++) {
                    double x = pos[i * 3], y = pos[i * 3 + 1], z = pos[i * 3 + 2];

                    float wx = (float)(world[0] * x + world[4] * y + world[8] * z + world[12]);
                    float wy = (float)(world[1] * x + world[5] * y + world[9] * z + world[13]);
                    float wz = (float)(world[2] * x + world[6] * y + world[10] * z + world[14]);

                    // glTF UVs have v=0 at the top of the image; OpenGL has it at the bottom
                    float u = uv[i * 2];
                    float v = 1.0f - uv[i * 2 + 1];

                    verts.push_back(wx); verts.push_back(wy); verts.push_back(wz);
                    verts.push_back(u);  verts.push_back(v);

                    out.minX = std::min(out.minX, wx); out.maxX = std::max(out.maxX, wx);
                    out.minY = std::min(out.minY, wy); out.maxY = std::max(out.maxY, wy);
                    out.minZ = std::min(out.minZ, wz); out.maxZ = std::max(out.maxZ, wz);
                }

                std::vector<uint32_t> idx;
                if (prim.indices >= 0) {
                    readIndices(gltf, prim.indices, idx);
                }
                else {
                    idx.resize(vcount);
                    for (size_t i = 0; i < vcount; i++) idx[i] = (uint32_t)i;
                }

                GLsizei first = (GLsizei)indices.size();
                for (uint32_t i : idx) indices.push_back(base + i);

                out.draws.push_back({ first, (GLsizei)idx.size(), getTexture(prim.material) });
            }
        }

        for (int child : node.children) visit(child, world);
        };

    Mat4d identity{};
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0;

    if (!gltf.scenes.empty()) {
        int si = gltf.defaultScene >= 0 ? gltf.defaultScene : 0;
        for (int root : gltf.scenes[si].nodes) visit(root, identity);
    }

    if (indices.empty()) {
        SDL_Log("No drawable triangles in %s", path);
        return false;
    }

    glGenVertexArrays(1, &out.vao);
    glGenBuffers(1, &out.vbo);
    glGenBuffers(1, &out.ebo);
    glBindVertexArray(out.vao);

    glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    return true;
}

void destroyModel(Model& m) {
    for (const auto& d : m.draws) glDeleteTextures(1, &d.texture);
    glDeleteBuffers(1, &m.ebo);
    glDeleteBuffers(1, &m.vbo);
    glDeleteVertexArrays(1, &m.vao);
    m = Model{};
}