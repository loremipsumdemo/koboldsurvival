#include "render/skybox.hpp"

#include <vector>

// stb_image: the game builds its implementation in model.cpp, so only the declarations are included here.
#include "stb_image.h"

namespace wm {


namespace {

// Face file names in the order of GL_TEXTURE_CUBE_MAP_POSITIVE_X + i.
const char* const kFaces[6] = {"posx", "negx", "posy", "negy", "posz", "negz"};
const char* const kExts[3] = {".jpg", ".jpeg", ".png"};

}  // namespace

bool Skybox::Load(const std::string& folder, std::string& err) {
    Unload();

    std::vector<unsigned char> pixels[6];
    int w = 0;
    int h = 0;
    for (int f = 0; f < 6; ++f) {
        unsigned char* img = nullptr;
        for (const char* ext : kExts) {
            const std::string path = folder + "/" + kFaces[f] + ext;
            int fw = 0, fh = 0, comp = 0;
            img = stbi_load(path.c_str(), &fw, &fh, &comp, 3);
            if (!img) continue;
            if (f == 0) {
                w = fw;
                h = fh;
            } else if (fw != w || fh != h) {
                stbi_image_free(img);
                err = std::string("skybox face ") + kFaces[f] + " is not the same size as posx";
                return false;
            }
            break;
        }
        if (!img) {
            err = "missing skybox face " + folder + "/" + kFaces[f] + ".jpg (or .png)";
            return false;
        }
        const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3;
        pixels[f].assign(img, img + bytes);
        stbi_image_free(img);
    }

    // Horizon colour: the middle rows of the four side faces, averaged.
    // Side faces are posx(0), negx(1), posz(4), negz(5).
    double sum[3] = {0.0, 0.0, 0.0};
    long long count = 0;
    const int band = h / 20 > 0 ? h / 20 : 1;
    const int from = h / 2 - band;
    const int to = h / 2 + band;
    for (int f : {0, 1, 4, 5}) {
        for (int y = from; y <= to; ++y) {
            if (y < 0 || y >= h) continue;
            for (int x = 0; x < w; ++x) {
                const unsigned char* p = &pixels[f][(static_cast<std::size_t>(y) * w + x) * 3];
                sum[0] += p[0];
                sum[1] += p[1];
                sum[2] += p[2];
                ++count;
            }
        }
    }
    if (count > 0) {
        horizon_ = {static_cast<float>(sum[0] / (count * 255.0)),
                    static_cast<float>(sum[1] / (count * 255.0)),
                    static_cast<float>(sum[2] / (count * 255.0))};
    }

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int f = 0; f < 6; ++f) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGB8, w, h, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, pixels[f].data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return true;
}

void Skybox::Unload() {
    if (tex_) glDeleteTextures(1, &tex_);
    tex_ = 0;
    horizon_ = {0.5f, 0.5f, 0.5f};
}

}  // namespace wm
