// Renderer: the loading screen, the title screen's backdrop, header, menu items and footer, and the party menu.
#include "renderer_internal.h"

using namespace render_detail;

namespace {

// Smooth 2D noise, as the title backdrop's edges use: Perlin's gradient noise over a shuffled table, seeded so each
// edge has its own shape. Returns about -0.6 to 0.6.
struct EdgeNoise {
    unsigned char p[512];
    explicit EdgeNoise(unsigned seed) {
        unsigned char perm[256];
        for (int i = 0; i < 256; ++i) perm[i] = (unsigned char)i;
        unsigned st = seed;
        auto rnd = [&]() { st = st * 1664525u + 1013904223u; return st / 4294967296.0; };
        for (int i = 255; i > 0; --i) {
            const int j = (int)(rnd() * (i + 1));
            std::swap(perm[i], perm[j]);
        }
        for (int i = 0; i < 512; ++i) p[i] = perm[i & 255];
    }
    float operator()(float x, float y) const {
        const int X = (int)std::floor(x) & 255, Y = (int)std::floor(y) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
        auto lerp = [](float a, float b, float t) { return a + t * (b - a); };
        auto grad = [](int h, float gx, float gy) { return ((h & 1) ? -gx : gx) + ((h & 2) ? -gy : gy); };
        const float u = fade(x), v = fade(y);
        const int A = p[X] + Y, B = p[X + 1] + Y;
        return lerp(lerp(grad(p[A], x, y), grad(p[B], x - 1.0f, y), u),
                    lerp(grad(p[A + 1], x, y - 1.0f), grad(p[B + 1], x - 1.0f, y - 1.0f), u), v) * 0.62f;
    }
};

// A colour from stops at 0..1 (stops in order)
RGBA stopColor(const std::vector<std::pair<float, RGBA>>& stops, float t) {
    if (t <= stops.front().first) return stops.front().second;
    for (size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].first) {
            const float span = stops[i].first - stops[i - 1].first;
            return mixColor(stops[i - 1].second, stops[i].second, span > 0.0f ? (t - stops[i - 1].first) / span : 1.0f);
        }
    }
    return stops.back().second;
}

// One side of one band: where its edge sits, how much it wanders, and how fast
struct BandEdge {
    int layer;
    int side;   // -1 the left side, 1 the right
    float base, a1, a2, f1, f2, m1, m2;
    EdgeNoise noise;
};

} // namespace

// Loading screen: the title screen's moving backdrop, with a rounded progress bar across the middle, the stage that is
// running above it and how far along it is below
void Renderer::drawLoadingScreen(float progress, int winW, int winH, const std::string& label, const std::string& category,
                                 bool backdrop) {
    if (!backdrop) {
        // Plain dark behind the bar
        glViewport(0, 0, winW, winH);
        glClearColor(0.055f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    beginUi(winW, winH);
    if (backdrop) drawTitleBackground();
    const float W = (float)winW, H = (float)winH;
    float s = H / 720.0f;
    const float p = std::clamp(progress, 0.0f, 1.0f);

    // At start-up, the loading picture: 800 x 600 in the middle of the window (shrunk to fit a smaller one) inside an
    // 8 pixel black border, filled without stretching (a wider picture loses a little off its sides). The bar runs
    // across the bottom of the picture, with what is loading and the share done under the border.
    const bool art = !backdrop && loadingArt_.tex;
    Rect pic;
    if (art) {
        constexpr float PIC_W = 800.0f, PIC_H = 600.0f, PIC_BORDER = 8.0f;
        const float textRoom = 2.0f * uiTextHeight("Ag") + 40.0f;   // the lines under the picture
        const float fit = std::min({ 1.0f, (W - 2.0f * PIC_BORDER - 32.0f) / PIC_W,
                                     (H - 2.0f * PIC_BORDER - 32.0f - 2.0f * textRoom) / PIC_H });
        const float pw = PIC_W * std::max(0.1f, fit), ph = PIC_H * std::max(0.1f, fit);
        pic = Rect{ std::floor((W - pw) * 0.5f), std::floor((H - ph) * 0.5f), pw, ph };
        drawRect(Rect{ pic.x - PIC_BORDER, pic.y - PIC_BORDER, pic.w + 2.0f * PIC_BORDER, pic.h + 2.0f * PIC_BORDER },
                 RGBA{ 0.0f, 0.0f, 0.0f, 1.0f });
        // The part of the picture that fills the frame at its own shape
        float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
        const float frameAspect = pic.w / pic.h;
        if (loadingArt_.aspect > frameAspect) {
            const float keep = frameAspect / loadingArt_.aspect;
            u0 = (1.0f - keep) * 0.5f;
            u1 = 1.0f - u0;
        } else if (loadingArt_.aspect < frameAspect) {
            const float keep = loadingArt_.aspect / frameAspect;
            v0 = (1.0f - keep) * 0.5f;
            v1 = 1.0f - v0;
        }
        drawUiQuad(loadingArt_.tex, pic.x, pic.y, pic.x + pic.w, pic.y + pic.h, u0, v0, u1, v1, WHITE);
        s = pic.h / 600.0f;   // the bar keeps its size against the picture
    }

    const float barW = art ? pic.w - 32.0f * s : std::min(W * 0.6f, 760.0f * s), barH = 26.0f * s;
    const Rect outer = art ? Rect{ pic.x + 16.0f * s, pic.y + pic.h - 16.0f * s - barH, barW, barH }
                           : Rect{ (W - barW) * 0.5f, (H - barH) * 0.5f, barW, barH };
    const float border = std::max(2.0f, 2.0f * s), gap = std::max(2.0f, 3.0f * s);
    // A soft glow, the pink border, the dark inside, then the green fill
    drawRoundedRect(Rect{ outer.x - 6.0f * s, outer.y - 6.0f * s, outer.w + 12.0f * s, outer.h + 12.0f * s },
                    (outer.h + 12.0f * s) * 0.5f, theme::alpha(theme::PINK, 0.12f));
    drawRoundedRect(outer, outer.h * 0.5f, theme::PINK);
    const Rect inside{ outer.x + border, outer.y + border, outer.w - 2.0f * border, outer.h - 2.0f * border };
    drawRoundedRect(inside, inside.h * 0.5f, theme::alpha(theme::NIGHT, 0.96f));
    const Rect track{ inside.x + gap, inside.y + gap, inside.w - 2.0f * gap, inside.h - 2.0f * gap };
    if (p > 0.0f) {
        const Rect fill{ track.x, track.y, std::max(track.h, track.w * p), track.h };
        drawRoundedRect(fill, fill.h * 0.5f, theme::GOLD);
        // A lighter line along its top, so the fill reads as lit
        drawRoundedRect(Rect{ fill.x + fill.h * 0.3f, fill.y + fill.h * 0.18f, std::max(0.0f, fill.w - fill.h * 0.6f), fill.h * 0.22f },
                        fill.h * 0.11f, RGBA{ 1.0f, 1.0f, 1.0f, 0.28f });
    }

    // The stage above the bar (above the picture's border, with the picture), and the share done below it (below the
    // border), then what is loading now
    const float above = art ? pic.y - 8.0f : outer.y;
    const float below = art ? pic.y + pic.h + 8.0f : outer.y + outer.h;
    if (!label.empty()) {
        drawUiTextCentered(label, W * 0.5f, above - 14.0f * s - uiTextHeight("Ag"), theme::BONE);
    }
    const std::string pct = std::to_string((int)std::lround(p * 100.0f)) + "%";
    drawUiTextCentered(pct, W * 0.5f, below + 12.0f * s, theme::ASH);
    if (!category.empty()) {
        drawUiTextCentered(category, W * 0.5f, below + 18.0f * s + uiTextHeight("Ag"), theme::BONE);
    }
    endUi();
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawTitleBackground() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float W = (float)uiW_, H = (float)uiH_;
    if (W <= 0.0f || H <= 0.0f) return;
    const float s = H / 720.0f;
    const float t = (float)(SDL_GetTicks() % 100000000ULL);   // milliseconds

    // The mouse sways the bands a little, eased toward where it is
    {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        const float target = std::clamp((mx / W - 0.5f) * 2.0f, -1.0f, 1.0f);
        const unsigned long long now = SDL_GetTicks();
        const float frames = titleMouseAt_ == 0 ? 0.0f : std::min(10.0f, (float)(now - titleMouseAt_) / 16.7f);
        titleMouse_ += (target - titleMouse_) * (1.0f - std::pow(1.0f - 0.07f, frames));
        titleMouseAt_ = now;
    }

    // A grid of corners, each coloured by fn, as triangles
    auto mesh = [&](int cols, int rows, auto fn) {
        std::vector<UiVert> v;
        v.reserve((size_t)cols * rows * 6);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const float x0 = W * c / cols, x1 = W * (c + 1) / cols;
                const float y0 = H * r / rows, y1 = H * (r + 1) / rows;
                const UiVert a{ x0, y0, fn(x0, y0) }, b{ x1, y0, fn(x1, y0) }, cc{ x1, y1, fn(x1, y1) }, d{ x0, y1, fn(x0, y1) };
                v.insert(v.end(), { a, b, cc, a, cc, d });
            }
        }
        drawUiTris(v);
    };

    // The night: a deep coral glow high in the middle, fading to black at the edges
    const std::vector<std::pair<float, RGBA>> sky{ { 0.0f, rgb255(40, 16, 22, 1.0f) }, { 0.58f, rgb255(16, 7, 9, 1.0f) },
                                                   { 1.0f, rgb255(6, 3, 3, 1.0f) } };
    const RGBA purple = rgb255(232, 96, 110, 1.0f);
    mesh(32, 18, [&](float x, float y) {
        const float r1 = std::hypot((x - 0.5f * W) / (1.4f * W), (y - 0.38f * H) / (1.1f * H));
        RGBA c = stopColor(sky, r1);
        const float r2 = std::hypot((x - 0.5f * W) / (0.8f * W), (y - 0.26f * H) / (0.55f * H));
        const float a = 0.22f * std::clamp(1.0f - r2 / 0.65f, 0.0f, 1.0f);
        c = mixColor(c, purple, a);
        c.a = 1.0f;
        return c;
    });

    // The bands: gold further in, coral over it nearer each side. Their edges wander, and drift up the screen.
    static BandEdge edges[4] = {
        { 0, -1, 0.335f, 0.082f, 0.020f, 0.0019f, 0.0079f, 0.000130f, 0.00037f, EdgeNoise(10771) },
        { 0, 1, 0.355f, 0.074f, 0.024f, 0.0023f, 0.0068f, 0.000094f, 0.00051f, EdgeNoise(55219) },
        { 1, -1, 0.185f, 0.058f, 0.015f, 0.0027f, 0.0096f, 0.000171f, 0.00029f, EdgeNoise(90413) },
        { 1, 1, 0.168f, 0.065f, 0.013f, 0.0016f, 0.0110f, 0.000212f, 0.00044f, EdgeNoise(31867) },
    };
    struct Layer {
        float speed, mouse;
        std::vector<std::pair<float, RGBA>> fill;
        RGBA stroke;
        float lw;
        RGBA glow;
        float gb;
    };
    const Layer layers[2] = {
        { 0.050f, 10.0f, { { 0.0f, rgb255(232, 200, 150, 0.92f) }, { 0.55f, rgb255(150, 110, 70, 0.9f) }, { 1.0f, rgb255(40, 24, 12, 0.92f) } },
          rgb255(245, 201, 117, 0.95f), 2.0f, rgb255(245, 201, 117, 0.55f), 16.0f },
        { 0.019f, 28.0f, { { 0.0f, rgb255(150, 50, 66, 0.96f) }, { 0.6f, rgb255(76, 22, 32, 0.96f) }, { 1.0f, rgb255(20, 6, 9, 0.97f) } },
          rgb255(232, 96, 110, 0.9f), 2.5f, rgb255(232, 96, 110, 0.5f), 14.0f },
    };
    const float S = std::min(W, 1500.0f);
    const float step = std::max(4.0f, 9.0f * s);
    for (int li = 0; li < 2; ++li) {
        const Layer& L = layers[li];
        const float scroll = -t * L.speed;
        const float push = titleMouse_ * L.mouse * s;
        for (const BandEdge& e : edges) {
            if (e.layer != li) continue;
            std::vector<std::pair<float, float>> pts;   // x, y
            for (float y = -step * 2.0f; y <= H + step * 2.0f; y += step) {
                const float yy = y / s * 1.25f + scroll;   // in the units the shapes were drawn for (a 900-high screen)
                const float n1 = e.noise(yy * e.f1, t * e.m1);
                const float n2 = e.noise(yy * e.f2 + 400.0f, t * e.m2 + 90.0f);
                const float v = e.base * W + n1 * e.a1 * S + n2 * e.a2 * S;
                pts.push_back({ e.side < 0 ? v + push : W - v + push, y });
            }
            const float off = e.side < 0 ? -2.0f : W + 2.0f;
            std::vector<UiVert> fill;
            for (size_t i = 1; i < pts.size(); ++i) {
                const auto [xa, ya] = pts[i - 1];
                const auto [xb, yb] = pts[i];
                const RGBA ca = stopColor(L.fill, ya / H), cb = stopColor(L.fill, yb / H);
                fill.insert(fill.end(), { { xa, ya, ca }, { xb, yb, cb }, { off, yb, cb }, { xa, ya, ca }, { off, yb, cb }, { off, ya, ca } });
            }
            drawUiTris(fill);
            // The edge line and its glow: a soft strip either side of it, then the line
            std::vector<UiVert> line;
            auto strip = [&](float width, RGBA mid, bool soft) {
                const float hw = width * 0.5f * s;
                const RGBA edgeC = soft ? theme::alpha(mid, 0.0f) : mid;
                for (size_t i = 1; i < pts.size(); ++i) {
                    const auto [xa, ya] = pts[i - 1];
                    const auto [xb, yb] = pts[i];
                    line.insert(line.end(), { { xa - hw, ya, edgeC }, { xa, ya, mid }, { xb, yb, mid },
                                              { xa - hw, ya, edgeC }, { xb, yb, mid }, { xb - hw, yb, edgeC },
                                              { xa, ya, mid }, { xa + hw, ya, edgeC }, { xb + hw, yb, edgeC },
                                              { xa, ya, mid }, { xb + hw, yb, edgeC }, { xb, yb, mid } });
                }
            };
            strip(L.lw + L.gb * 1.6f, theme::alpha(L.glow, L.glow.a * 0.45f), true);
            strip(L.lw + L.gb * 0.5f, theme::alpha(L.glow, L.glow.a * 0.8f), true);
            strip(L.lw, L.stroke, false);
            drawUiTris(line);
        }
    }

    // The veil (darker at the top and much darker at the bottom) and the vignette, as one layer of black
    const std::vector<std::pair<float, RGBA>> veil{ { 0.0f, RGBA{ 0, 0, 0, 0.6f } }, { 0.38f, RGBA{ 0, 0, 0, 0.12f } },
                                                    { 1.0f, RGBA{ 0, 0, 0, 0.85f } } };
    mesh(32, 18, [&](float x, float y) {
        const float a1 = stopColor(veil, y / H).a;
        const float r = std::hypot((x - 0.5f * W) / (1.18f * W), (y - 0.48f * H) / (0.78f * H));
        const float a2 = r < 0.34f ? 0.0f : 0.88f * std::min(1.0f, (r - 0.34f) / 0.66f);
        return RGBA{ 0.012f, 0.012f, 0.024f, 1.0f - (1.0f - a1) * (1.0f - a2) };
    });

    // Scan lines: a faint dark line every third pixel
    std::vector<UiVert> lines;
    lines.reserve((size_t)(H / 3.0f + 2.0f) * 6);
    const RGBA scan{ 0.0f, 0.0f, 0.0f, 0.17f };
    for (float y = 0.0f; y < H; y += 3.0f) {
        lines.insert(lines.end(), { { 0, y, scan }, { W, y, scan }, { W, y + 1.0f, scan }, { 0, y, scan }, { W, y + 1.0f, scan }, { 0, y + 1.0f, scan } });
    }
    drawUiTris(lines);
}

void Renderer::drawTitleHeader(const TitleLayout& L, const std::string& firstWord, const std::string& secondWord,
                               const std::vector<std::string>& news) {
    const float s = uiScale();
    const float sway = -titleMouse_ * 5.0f * s;   // the words lean against the backdrop's sway
    const unsigned long long now = SDL_GetTicks();

    // The logo, as large as the name's line allows at its own shape, against the left margin. Now and then, for a
    // moment, it jolts sideways, as the words would glitch.
    if (titleLogo_.tex) {
        float h = L.name.h, w = titleLogo_.aspect * h;
        if (w > L.name.w) {
            w = L.name.w;
            h = w / titleLogo_.aspect;
        }
        const float phase = (float)(now % 11000ULL) / 11000.0f;
        float jolt = 0.0f;
        if (phase >= 0.974f && phase < 0.978f) jolt = -3.0f;
        else if (phase >= 0.978f && phase < 0.982f) jolt = 3.0f;
        else if (phase >= 0.986f && phase < 0.990f) jolt = -1.0f;
        const float x = L.name.x + sway + jolt * s, y = L.name.y + (L.name.h - h) * 0.5f;
        drawTexture(titleLogo_.tex, x, y, w, h, WHITE);
    }

    // Without the logo, the name in words: the first word green, a pink bar, the second word bone-white, in capitals,
    // as large as the line allows. The words are pictures drawn once in the very large title font and shrunk to size,
    // so they stay sharp; without that font they fall back to the UI font, enlarged.
    if (!titleLogo_.tex) {
        const std::string a = uiUpper(firstWord), b = uiUpper(secondWord);
        const TextTex& ta = titleWord(a);
        const TextTex& tb = titleWord(b);
        const bool big = ta.tex && tb.tex;
        float lineH = L.name.h * 1.3f;                       // the height of a line of the name, as drawn
        const float baseH = std::max(1.0f, uiTextHeight("Ag"));
        auto wordW = [&](const TextTex& t, const std::string& text, float h) {
            return big ? t.aspect * h : uiTextWidth(text) * h / baseH;
        };
        float em = lineH / 1.36f;                            // the font's size, near enough
        const float total = wordW(ta, a, lineH) + wordW(tb, b, lineH) + em * (0.09f + 0.44f);
        if (total > L.name.w) {
            lineH *= L.name.w / total;
            em = lineH / 1.36f;
        }
        const float wa = wordW(ta, a, lineH);
        const float barW = std::max(2.0f, em * 0.09f), barH = em * 0.72f, barGap = em * 0.22f;
        // Now and then, for a moment, the name glitches: a jolt sideways and a pink and green ghost either side
        const float phase = (float)(now % 11000ULL) / 11000.0f;
        float jolt = 0.0f, pinkOff = 0.0f, greenOff = 0.0f;
        if (phase >= 0.974f && phase < 0.978f) { jolt = -3.0f; pinkOff = 3.0f; greenOff = -3.0f; }
        else if (phase >= 0.978f && phase < 0.982f) { jolt = 3.0f; pinkOff = -4.0f; greenOff = 2.0f; }
        else if (phase >= 0.986f && phase < 0.990f) { jolt = -1.0f; pinkOff = 2.0f; }
        const float x0 = L.name.x + sway + jolt * s;
        const float cy = L.name.y + L.name.h * 0.5f;
        const float top = cy - lineH * 0.5f;
        const float barX = x0 + wa + barGap;
        const float xb = barX + barW + barGap;
        // One word at its left edge and the line's top
        auto word = [&](const TextTex& t, const std::string& text, float left, float dx, float dy, RGBA tint) {
            if (big) {
                const float w = t.aspect * lineH, x = left + dx, y = top + dy;
                drawTexture(t.tex, x, y, w, lineH, tint);
            } else {
                drawUiTextLarge(text, left + wordW(t, text, lineH) * 0.5f + dx, cy + dy, lineH / baseH, tint);
            }
        };
        auto words = [&](float dx, float dy, RGBA ca, RGBA cb) {
            word(ta, a, x0, dx, dy, ca);
            word(tb, b, xb, dx, dy, cb);
        };
        // A dark drop below, then the glitch ghosts, a green glow round the first word, and the words
        words(0.0f, 6.0f * s, RGBA{ 0.0f, 0.0f, 0.0f, 0.55f }, RGBA{ 0.0f, 0.0f, 0.0f, 0.55f });
        if (pinkOff != 0.0f) words(pinkOff * s, 0.0f, theme::alpha(theme::PINK, 0.8f), theme::alpha(theme::PINK, 0.8f));
        if (greenOff != 0.0f) words(greenOff * s, 0.0f, theme::alpha(theme::GOLD, 0.8f), theme::alpha(theme::GOLD, 0.8f));
        for (int k = 0; k < 4; ++k) {
            const float gx = (k == 0 ? -2.0f : k == 1 ? 2.0f : 0.0f) * s, gy = (k == 2 ? -2.0f : k == 3 ? 2.0f : 0.0f) * s;
            word(ta, a, x0, gx, gy, theme::alpha(theme::GOLD, 0.14f));
        }
        words(0.0f, 0.0f, theme::GOLD, theme::BONE);
        // The bar: hot pink, with a glow, standing on the words' baseline
        const float barY = cy + barH * 0.5f - barH * 0.98f + em * 0.06f;
        drawRect(Rect{ barX - 4.0f * s, barY - 4.0f * s, barW + 8.0f * s, barH + 8.0f * s }, theme::alpha(theme::PINK, 0.22f));
        drawRect(Rect{ barX, barY, barW, barH }, theme::PINK);
    }

    // The rule: pink, fading out to the right
    drawRectGradientH(Rect{ L.rule.x + sway, L.rule.y, L.rule.w, L.rule.h }, theme::PINK, theme::alpha(theme::PINK, 0.0f));

    // The notice board: a pink frame, cut at the bottom right, over a dark pink inside with faint lines across it
    const Rect board{ L.news.x + sway, L.news.y, L.news.w, L.news.h };
    {
        const float frame = 3.0f * s, cut = 22.0f * s;
        std::vector<UiVert> v;
        cutCornerShape(v, Rect{ board.x + 2.0f * s, board.y + 14.0f * s, board.w, board.h }, cut, RGBA{ 0, 0, 0, 0.55f },
                       RGBA{ 0, 0, 0, 0.55f });   // its shadow
        cutCornerShape(v, board, cut, theme::PINK, theme::PINK_DEEP);
        const Rect in{ board.x + frame, board.y + frame, board.w - 2.0f * frame, board.h - 2.0f * frame };
        cutCornerShape(v, in, cut - frame, theme::PINK_DARK, theme::PINK_INK);
        const RGBA faint{ 1.0f, 1.0f, 1.0f, 0.035f };
        for (float y = in.y; y < in.y + in.h; y += 4.0f * s) {
            // The lines stop at the cut corner
            const float into = y - (in.y + in.h - (cut - frame));
            const float w = into > 0.0f ? in.w - into : in.w;
            if (w <= 0.0f) break;
            v.insert(v.end(), { { in.x, y, faint }, { in.x + w, y, faint }, { in.x + w, y + 1.0f, faint },
                                { in.x, y, faint }, { in.x + w, y + 1.0f, faint }, { in.x, y + 1.0f, faint } });
        }
        drawUiTris(v);
    }
    const float th = listTextHeight("Ag");
    const float textX = L.newsText.x + sway;
    float y = L.newsText.y;
    const float bottom = L.newsText.y + L.newsText.h;
    for (const std::string& raw : news) {
        if (raw.empty()) {
            y += th * 0.6f;
            continue;
        }
        const bool heading = raw.rfind("# ", 0) == 0;
        const bool bullet = raw.rfind("- ", 0) == 0;
        const std::string text = heading || bullet ? raw.substr(2) : raw;
        if (heading) {
            // A heading: pink capitals, with a pink line under them
            if (y + th > bottom) break;
            drawListText(uiUpper(text), textX, y, theme::PINK);
            y += th + 5.0f * s;
            drawRect(Rect{ textX, y, L.newsText.w, std::max(1.0f, 1.0f * s) }, theme::alpha(theme::PINK, 0.45f));
            y += 9.0f * s;
            continue;
        }
        // A point: a small green arrowhead, then the text, wrapped under itself. A point that doesn't fit whole is left
        // off, with the rest after it.
        const float indent = bullet ? th * 1.2f : 0.0f;
        const std::vector<std::string> wrapped = wrapListText(text, L.newsText.w - indent);
        if (y + th * (float)wrapped.size() > bottom) return;
        bool first = true;
        for (const std::string& ln : wrapped) {
            if (bullet && first) {
                const float cx = textX + 2.0f * s, cyy = y + th * 0.5f, h = 4.0f * s;
                drawUiTris({ { cx, cyy - h, theme::alpha(theme::GOLD, 0.9f) }, { cx + h * 1.4f, cyy, theme::alpha(theme::GOLD, 0.9f) },
                             { cx, cyy + h, theme::alpha(theme::GOLD, 0.9f) } });
            }
            drawListText(ln, textX + indent, y, theme::NOTICE_TEXT);
            y += th;
            first = false;
        }
        y += th * 0.35f;
    }
}

void Renderer::drawMenuItem(const Rect& r0, const std::string& label, const std::string& sub, bool selected, bool enabled,
                            float appear) {
    const float s = uiScale();
    appear = std::clamp(appear, 0.0f, 1.0f);
    const float fade = appear;
    if (selected && enabled) noteHover(r0);
    Rect r = r0;
    r.x += (selected && enabled ? -14.0f * s : 0.0f) + (1.0f - appear) * 34.0f * s;   // lit items step out; new ones slide in
    auto A = [&](RGBA c) { return theme::alpha(c, c.a * fade); };
    // The band: dark, clearing to nothing on the left; lit, it turns green
    const float split = r.x + r.w * (selected ? 0.40f : 0.45f);
    const RGBA left = selected ? rgb255(245, 201, 117, 0.0f) : rgb255(22, 10, 12, 0.0f);
    const RGBA mid = selected ? rgb255(52, 30, 16, 0.75f) : rgb255(22, 10, 12, 0.72f);
    const RGBA right = selected ? rgb255(96, 58, 30, 0.9f) : rgb255(22, 10, 12, 0.9f);
    drawRectGradientH(Rect{ r.x, r.y, split - r.x, r.h }, A(left), A(mid));
    drawRectGradientH(Rect{ split, r.y, r.x + r.w - split, r.h }, A(mid), A(right));
    if (selected) {
        // A green glow along its right end, and a soft green halo
        const float gw = r.w * 0.46f;
        drawRectGradientH(Rect{ r.x + r.w - gw, r.y, gw, r.h }, A(rgb255(245, 201, 117, 0.0f)), A(rgb255(245, 201, 117, 0.16f)));
        drawRect(Rect{ r.x - 4.0f * s, r.y - 3.0f * s, r.w + 8.0f * s, r.h + 6.0f * s }, A(rgb255(245, 201, 117, 0.05f)));
    }
    // The edge on the left
    const RGBA edge = selected ? theme::GOLD : theme::alpha(theme::ASH, enabled ? 0.35f : 0.15f);
    drawRect(Rect{ r.x, r.y, 3.0f * s, r.h }, A(edge));
    if (selected) drawRect(Rect{ r.x - 1.0f * s, r.y, 1.0f * s, r.h }, A(theme::GOLD));
    // The words, in capitals
    const RGBA ink = !enabled ? theme::alpha(theme::ASH, 0.4f) : selected ? theme::BONE : theme::ASH;
    const std::string up = uiUpper(label);
    const float lh = uiTextHeight(up);
    const float subH = sub.empty() ? 0.0f : listTextHeight("Ag");
    const float block = lh + (sub.empty() ? 0.0f : 2.0f * s + subH);
    const float tx = r.x + 3.0f * s + 16.0f * s;
    const float ty = r.y + (r.h - block) * 0.5f;
    const float maxW = r.w - 3.0f * s - 28.0f * s;
    if (uiTextWidth(up) > maxW) {
        drawUiTextInBox(up, Rect{ tx, ty, maxW, lh }, A(ink));
    } else {
        drawUiText(up, tx, ty, A(ink));
    }
    if (!sub.empty()) {
        const RGBA subInk = selected ? rgb255(245, 201, 117, 0.85f) : theme::alpha(theme::ASH, 0.6f);
        drawListText(fitText(sub, maxW, false), tx, ty + lh + 2.0f * s, A(subInk));
    }
}

void Renderer::drawTitleFooter(const Rect& f, const std::string& left, const std::string& right) {
    const float s = uiScale();
    const RGBA ink = theme::alpha(theme::ASH, 0.75f);
    const float th = listTextHeight("Ag");
    const float y = f.y + (f.h - th) * 0.5f;
    drawListText(uiUpper(left), f.x, y, ink);
    // The arrow keys as two small arrowheads, up and down, then the words
    const std::string words = uiUpper(right);
    const float ww = listTextWidth(words);
    const float h = 4.0f * s, w = 4.5f * s, gap = 4.0f * s;
    const float x = f.x + f.w - ww;
    const float ax = x - (4.0f * w + gap) - 8.0f * s, cy = f.y + f.h * 0.5f;
    drawUiTris({ { ax, cy + h, ink }, { ax + w, cy - h, ink }, { ax + 2.0f * w, cy + h, ink },
                 { ax + 2.0f * w + gap, cy - h, ink }, { ax + 3.0f * w + gap, cy + h, ink }, { ax + 4.0f * w + gap, cy - h, ink } });
    drawListText(words, x, y, ink);
}

void Renderer::drawPartySetup(const std::vector<Button>& buttons, const Party& p, const PartyUi& ui,
                              const PartyPanels& P, float mx, float my) {
    const float s = uiScale();
    const RGBA faint{ 0.50f, 0.43f, 0.40f, 1.0f };
    const RGBA panel = theme::alpha(theme::PANEL_FILL, 0.92f);
    const RGBA plate = theme::alpha(theme::SLOT, 0.95f);
    const RGBA plateHot = theme::alpha(theme::SLOT_HOT, 0.95f);
    const RGBA plateOn = theme::alpha(theme::GOLD_DEEP, 0.95f);    // the purple used for the selected unit
    const RGBA plateOff = theme::alpha(theme::BOX_FILL, 0.70f);
    const float menuLineH = listTextHeight("Ag");
    float lineH = menuLineH;   // changes to the game's size for the Stats tab's rows
    const PartyMember& picked = pickedMember(p, ui);

    // Background for a plate: purple when chosen, lighter when hovered, dim when disabled
    auto plateFor = [&](bool hot, bool on, bool enabled) -> RGBA {
        if (!enabled) return plateOff;
        if (on) return plateOn;
        return hot ? plateHot : plate;
    };
    // Text centerd in a box, shortened to fit
    auto centerd = [&](const Rect& r, const std::string& text, RGBA tint) {
        const std::string t = fitText(text, r.w - 8.0f * s, false);
        drawListText(t, r.x + (r.w - listTextWidth(t)) * 0.5f, r.y + (r.h - lineH) * 0.5f, tint);
    };
    // Text at the left of a box, vertically centerd, after an inset
    auto leftText = [&](const Rect& r, const std::string& text, RGBA tint, float inset) {
        const std::string t = fitTextTip(text, r.w - inset - 6.0f * s, r);
        drawListText(t, r.x + inset, r.y + (r.h - lineH) * 0.5f, tint);
    };

    drawRect(P.left, panel);
    drawRect(P.center, panel);
    drawRect(P.right, panel);
    drawRect(P.bottom, theme::alpha(theme::WELL, 0.95f));
    // The unit list's scroll bar, when it has more units than fit
    const PartyListBar bar = partyListBar((int)uiW_, (int)uiH_, (int)p.size(), ui.listScroll);
    if (bar.track.h > 0.0f) drawScrollBar(bar.track, bar.thumb, ui.listDrag || bar.thumb.contains(mx, my));

    const bool menuSet = text_.menuText();
    for (const Button& b : buttons) {
        // The Stats tab's rows (headings, stats and their - and +) are drawn in the game's fonts: there are many of them
        const bool statRow = b.action == Action::PartyStatLabel || b.action == Action::PartyStep
                          || b.action == Action::PartyHeading;
        setMenuText(menuSet && !statRow);
        lineH = statRow ? listTextHeight("Ag") : menuLineH;
        const bool hot = b.enabled && b.rect.contains(mx, my);
        if (hot) noteHover(b.rect);
        switch (b.action) {
        case Action::PartyPick: {
            const PartyMember& u = p[static_cast<size_t>(b.param)];
            const bool chosen = b.param == std::clamp(ui.selected, 0, std::max(0, (int)p.size() - 1));
            drawRect(b.rect, plateFor(hot, chosen, true));
            const float pic = b.rect.h - 14.0f * s;
            const Rect picRect{ b.rect.x + 7.0f * s, b.rect.y + 7.0f * s, pic, pic };
            if (u.mob != MobKind::None) drawFitted(mobPicture(u.mob), picRect, WHITE);
            else drawUnitPicture(u.sprite, picRect);
            const float tx = b.rect.x + pic + 16.0f * s, tw = b.rect.x + b.rect.w - tx - 8.0f * s;
            const bool named = !cleanName(u.name).empty();
            const std::string shown = named ? cleanName(u.name) : std::string(lang("UILABEL_NO_NAME"));
            drawListText(fitText(shown, tw, false), tx, b.rect.y + (b.rect.h - lineH) * 0.5f,
                         !named ? theme::WARN_TEXT : chosen ? theme::GOLD : WHITE);
            break;
        }

        case Action::PartyTabPick: {
            const bool on = b.param == (int)ui.tab;
            drawRect(b.rect, plateFor(hot, on, true));
            drawIconLabel(b.rect, partyTabIcon(b.param), b.label, on ? theme::GOLD : WHITE, false);
            break;
        }

        case Action::PartyNameField: {
            drawListText(lang("UILABEL_NAME"), b.rect.x, b.rect.y - 22.0f * s, theme::TEXT_QUIET);
            drawRect(b.rect, plateFor(hot, ui.editingName, true));
            if (b.label.empty() && !ui.editingName) {
                leftText(b.rect, lang("UILABEL_CLICK_TO_TYPE"), faint, 10.0f * s);
            } else {
                leftText(b.rect, b.label + (ui.editingName ? "|" : ""), WHITE, 10.0f * s);
            }
            break;
        }

        case Action::PartySpriteBox:
            {
                // The heading is the model label with no number: the word for Model alone
                std::string heading = langf("UILABEL_MODEL_N", { "" });
                while (!heading.empty() && heading.back() == ' ') heading.pop_back();
                drawListText(heading, b.rect.x, b.rect.y - 22.0f * s, theme::TEXT_QUIET);
            }
            drawRect(b.rect, plateFor(hot, ui.dropKind == DropKind::Sprite, true));
            leftText(b.rect, b.label, WHITE, 10.0f * s);
            break;

        case Action::PartyBodyBox:
            drawListText(lang("UILABEL_BODY_TYPE"), b.rect.x, b.rect.y - 22.0f * s, theme::TEXT_QUIET);
            drawRect(b.rect, plateFor(hot, ui.dropKind == DropKind::Body, true));
            leftText(b.rect, b.label, WHITE, 10.0f * s);
            break;

        case Action::PartyMobBox:
            drawListText(lang("UILABEL_MOB_KIND"), b.rect.x, b.rect.y - 22.0f * s, theme::TEXT_QUIET);
            drawRect(b.rect, plateFor(hot, ui.dropKind == DropKind::MobKind && ui.dropSlot >= 0, true));
            leftText(b.rect, b.label, b.enabled ? WHITE : theme::TEXT_QUIET, 10.0f * s);
            break;

        case Action::PartyRandomUnit:
            drawRect(b.rect, plateFor(hot, false, true));
            centerd(b.rect, b.label, WHITE);
            break;

        case Action::PartyAddUnit:
        case Action::PartyRemoveUnit:
        case Action::PartyAddMob:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? WHITE : faint);
            break;

        case Action::PartyGearSlot: {
            const bool open = ui.dropKind == DropKind::Gear && ui.dropSlot == b.param;
            drawRect(b.rect, plateFor(hot, open, true));
            // The item cell on the left, with the item's 64 x 64 picture; the names to its right
            const Rect cell{ b.rect.x + 4.0f, b.rect.y + std::floor((b.rect.h - ITEM_CELL_PX) * 0.5f), ITEM_CELL_PX, ITEM_CELL_PX };
            drawRect(cell, RGBA{ 0.0f, 0.0f, 0.0f, 0.30f });
            const int item = b.param >= 0 && b.param < GEAR_SLOTS ? picked.gear[(size_t)b.param] : 0;
            if (item != 0) drawItemPictureAt(cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f, item, 1.0f);
            const float inset = ITEM_CELL_PX + 4.0f + 8.0f * s;
            leftText(Rect{ b.rect.x, b.rect.y + 6.0f * s, b.rect.w, lineH }, gearSlotName(b.param), theme::TEXT_QUIET, inset);
            leftText(Rect{ b.rect.x, b.rect.y + b.rect.h - lineH - 6.0f * s, b.rect.w, lineH }, b.label,
                     b.label == lang("UILABEL_EMPTY") ? faint : WHITE, inset);
            break;
        }

        case Action::PartyStatLabel: {
            drawRect(b.rect, RGBA{ 0.0f, 0.0f, 0.0f, 0.18f });
            const float isz = b.rect.h - 4.0f * s;
            drawUiIcon(statIcon((StatId)b.param), Rect{ b.rect.x + 4.0f * s, b.rect.y + 2.0f * s, isz, isz });
            leftText(Rect{ b.rect.x + isz + 8.0f * s, b.rect.y, b.rect.w - isz - 8.0f * s, b.rect.h }, b.label, WHITE, 2.0f * s);
            const std::string lv = std::to_string(levelOf(picked.stats, (StatId)b.param));
            drawListText(lv, b.rect.x + b.rect.w - 6.0f * s - listTextWidth(lv),
                         b.rect.y + (b.rect.h - lineH) * 0.5f, theme::GOLD);
            break;
        }

        case Action::PartyStep:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? WHITE : faint);
            break;

        case Action::PartyHeading:
            leftText(b.rect, b.label, theme::GOLD, 0.0f);
            break;

        case Action::PartyNote: {
            // A note with room for more than one line is wrapped, a line per paragraph and more as needed; a one-line
            // note is shortened to fit
            std::vector<std::string> lines;
            if (b.rect.h >= 2.0f * lineH) {
                size_t from = 0;
                while (from <= b.label.size()) {
                    const size_t nl = b.label.find('\n', from);
                    const std::string para = b.label.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
                    for (const std::string& l : wrapListText(para, b.rect.w)) lines.push_back(l);
                    if (nl == std::string::npos) break;
                    from = nl + 1;
                }
            }
            if (lines.size() <= 1) {
                leftText(b.rect, b.label, theme::TEXT_QUIET, 0.0f);
                break;
            }
            const float pitch = lineH * 1.05f;
            const int fit = std::max(1, (int)(b.rect.h / pitch));
            float y = b.rect.y;
            for (int k = 0; k < (int)lines.size() && k < fit; ++k, y += pitch) {
                drawListText(lines[(size_t)k], b.rect.x, y, theme::TEXT_QUIET);
            }
            break;
        }

        case Action::PartyInvSlot: {
            const ItemStack& st = picked.items[b.param];
            drawRect(b.rect, plateFor(hot, b.param == ui.stackSlot, true));
            if (st.item > 0) {
                drawItemPicture(b.rect, st.item, 1.0f);
                const std::string n = std::to_string(st.count);
                drawListText(n, b.rect.x + b.rect.w - listTextWidth(n) - 3.0f * s,
                             b.rect.y + b.rect.h - lineH - 2.0f * s, theme::TEXT_QUIET);
            }
            break;
        }

        case Action::PartyCount:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? WHITE : faint);
            break;

        case Action::PartyMessage:
            centerd(b.rect, b.label, partyNamesValid(p) ? theme::TEXT_QUIET : theme::WARN_TEXT);
            break;

        case Action::PartyDropBack:
            drawRect(Rect{ b.rect.x - 1.0f, b.rect.y - 1.0f, b.rect.w + 2.0f, b.rect.h + 2.0f }, RGBA{ 0.72f, 0.52f, 0.40f, 0.9f });
            drawRect(b.rect, theme::alpha(theme::MENU_FILL, 0.98f));
            break;

        case Action::PartyDropScroll:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? (hot ? theme::GOLD : WHITE) : faint);
            break;

        case Action::PartyDropPick:
            if (hot) drawRect(b.rect, plateHot);
            leftText(b.rect, b.label, hot ? theme::GOLD : WHITE, 10.0f * s);
            break;

        case Action::PartyRandomAll:
        case Action::PartyConfirm:
        case Action::Back:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? WHITE : faint);
            break;

        default:
            break;
        }
    }
    setMenuText(menuSet);
    lineH = menuLineH;

    // Middle: the picked unit, large, with its name and totals under it. With no units, only the message.
    if (p.empty()) {
        drawUiTextCentered(fitText(lang("UILABEL_NEEDS_UNITS"), P.center.w - 24.0f * s, true), P.center.x + P.center.w * 0.5f,
                           P.center.y + P.center.h * 0.5f, theme::WARN_TEXT);
        return;
    }
    const float cw = P.center.w;
    // The picked unit's model, turned by dragging in it with the right button
    if (picked.mob != MobKind::None) drawMobPreview(picked.mob, partyPreviewRect(uiW_, uiH_), ui.previewYaw);
    else drawUnitModelPreview(picked.sprite, partyPreviewRect(uiW_, uiH_), ui.previewYaw);
    const bool named = !cleanName(picked.name).empty();
    const std::string name = named ? cleanName(picked.name) : std::string(lang("UILABEL_NO_NAME"));
    drawUiTextCentered(fitText(name, cw - 24.0f * s, true), P.center.x + cw * 0.5f,
                       P.center.y + P.center.h - 100.0f * s, named ? WHITE : theme::WARN_TEXT);
    int attack = 0, defense = 0;
    gearTotals(wornBy(picked), attack, defense);
    const std::string totals = langf("UILABEL_ATTACK_DEFENSE_CARRY", { std::to_string(attack), std::to_string(defense), std::to_string((int)carryLimit(picked.stats)) });
    const std::string shownTotals = fitText(totals, cw - 24.0f * s, false);
    drawListText(shownTotals, P.center.x + (cw - listTextWidth(shownTotals)) * 0.5f,
                 P.center.y + P.center.h - 62.0f * s, theme::TEXT_QUIET);
}
