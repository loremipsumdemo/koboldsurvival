#pragma once
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include "camera.h"
#include "sky.h"
#include "ecs.h"
#include "factions.h"
#include "game_log.h"
#include "mobs.h"
#include "model.h"
#include "terrain_mesh.h"
#include "ui_types.h"
#include "props.h"
#include "overview.h"
#include "text_cache.h"

class LoadStages;
class Terrain;
struct FloatingNumber;
struct TradeLabel;
struct PartyMember;
struct PartyUi;
struct PartyPanels;
struct Stats;
enum class StatId : int;
struct BehaviorRule;
struct SpokenLine;
struct DialogueOption;
struct Equipment;
struct Inventory;
struct ItemStack;
struct Storage;
struct Output;
struct TradeGhost;
struct TradeStatus;
struct SidebarLayout;
struct UnitPanelLayout;
struct WorldMapLayout;
struct DialogueLayout;
struct LootLayout;
struct StorageLayout;
struct TradeLayout;
struct OptimizeAllLayout;
struct QuantityLayout;
struct DisbandLayout;
struct ItemMenuLayout;
struct TitleLayout;
struct PercentPromptLayout;

struct AlphaMask {
    int w = 0, h = 0;
    std::vector<unsigned char> alpha;
};

class PictureCache {
public:
    using FileOf = const std::string& (*)(int);
    explicit PictureCache(const char* prefix, bool keepMasks = false, FileOf fileOf = nullptr)
        : prefix_(prefix), keepMasks_(keepMasks), fileOf_(fileOf) {}
    const TextTex& get(int n);
    const AlphaMask& mask(int n);
    void clear();
private:
    const char* prefix_;
    bool keepMasks_;
    FileOf fileOf_;
    std::map<int, TextTex> pics_;
    std::map<int, AlphaMask> masks_;
};

struct RGBA {
    float r, g, b, a;
};

namespace theme {
inline constexpr RGBA PINK{ 0.910f, 0.376f, 0.431f, 1.0f };
inline constexpr RGBA PINK_DEEP{ 0.588f, 0.196f, 0.259f, 1.0f };
inline constexpr RGBA PINK_DARK{ 0.298f, 0.086f, 0.125f, 1.0f };
inline constexpr RGBA PINK_INK{ 0.098f, 0.027f, 0.039f, 1.0f };
inline constexpr RGBA GOLD{ 0.961f, 0.788f, 0.459f, 1.0f };
inline constexpr RGBA EMPTY_SLOT{ 1.0f, 1.0f, 1.0f, 0.12f };     // an empty gear slot's icon: faint, so it isn't taken for gear
inline constexpr RGBA BLOCKED_SLOT{ 1.0f, 1.0f, 1.0f, 0.06f };   // an off hand a two-handed weapon blocks
inline constexpr RGBA GOLD_DEEP{ 0.353f, 0.235f, 0.110f, 1.0f };
inline constexpr RGBA BONE{ 0.957f, 0.902f, 0.796f, 1.0f };
inline constexpr RGBA ASH{ 0.643f, 0.553f, 0.510f, 1.0f };
inline constexpr RGBA NOTICE_TEXT{ 0.871f, 0.776f, 0.690f, 1.0f };
inline constexpr RGBA NIGHT{ 0.082f, 0.039f, 0.047f, 1.0f };
inline constexpr RGBA TEXT_DIM{ 0.80f, 0.72f, 0.64f, 1.0f };
inline constexpr RGBA TEXT_QUIET{ 0.74f, 0.66f, 0.59f, 1.0f };
inline constexpr RGBA WELL{ 0.055f, 0.02f, 0.03f, 1.0f };
inline constexpr RGBA SLOT{ 0.14f, 0.06f, 0.075f, 1.0f };
inline constexpr RGBA SLOT_HOT{ 0.26f, 0.16f, 0.07f, 1.0f };
inline constexpr RGBA TAB_OFF{ 0.16f, 0.07f, 0.085f, 1.0f };
inline constexpr RGBA SCROLL_TRACK{ 0.20f, 0.09f, 0.11f, 0.9f };
inline constexpr RGBA SCRIM{ 0.0f, 0.0f, 0.0f, 0.45f };
inline constexpr RGBA PANEL_FILL{ 0.085f, 0.035f, 0.045f, 1.0f };
inline constexpr RGBA MENU_FILL{ 0.075f, 0.03f, 0.04f, 1.0f };
inline constexpr RGBA BOX_FILL{ 0.11f, 0.05f, 0.06f, 1.0f };
inline constexpr RGBA GOOD{ 0.42f, 0.95f, 0.45f, 1.0f };
inline constexpr RGBA BAD{ 1.0f, 0.38f, 0.36f, 1.0f };
inline constexpr RGBA LOG_TEXT{ 0.85f, 0.78f, 0.70f, 1.0f };
inline constexpr RGBA OK_TEXT{ 0.6f, 1.0f, 0.6f, 1.0f };
inline constexpr RGBA WARN_TEXT{ 1.0f, 0.72f, 0.55f, 1.0f };
inline constexpr RGBA AMBER{ 0.91f, 0.65f, 0.24f, 1.0f };
inline constexpr RGBA HEALTH_BAR{ 0.80f, 0.26f, 0.26f, 1.0f };
inline constexpr RGBA LEVEL_BAR{ 0.45f, 0.65f, 0.88f, 1.0f };
inline constexpr RGBA HURT{ 0.85f, 0.10f, 0.10f, 1.0f };
inline constexpr RGBA alpha(RGBA c, float a) { return RGBA{ c.r, c.g, c.b, a }; }
}

struct UiVert {
    float x, y;
    RGBA c;
};

struct HudEntry {
    std::string text;
    RGBA color;
};
inline constexpr RGBA HUD_UNIT_COLOR{ 1.0f, 1.0f, 1.0f, 1.0f };
inline constexpr RGBA HUD_ITEM_COLOR{ 1.0f, 0.72f, 0.2f, 1.0f };
inline constexpr RGBA HUD_PROP_COLOR{ 0.68f, 0.68f, 0.68f, 1.0f };

struct InfoTime {
    std::string clock;
    std::string date;
    std::string ymd;
    std::string days;
};

struct GameLogLayout {
    Rect panel;
    Rect bar;
    Rect track;
    Rect thumb;
    float textX = 0.0f, textW = 0.0f;
    float lineH = 0.0f;
    float rowsTop = 0.0f;   // where the first of the visible lines starts
    float starW = 0.0f;     // the Game tab's * button at the start of each line
    int linesVisible = 0;
    int lineCount = 0;
    int offset = 0;
    int first = 0;
    int shown = 0;

    bool collapsed = false;
    Rect toggle;
    Rect lock;
    float bodyTop = 0.0f, bodyBottom = 0.0f;
    int tab = 0;
    Rect tabMessage;
    Rect tabGame;
    Rect tabTime;
    Rect filterBtn;
    bool filterOpen = false;
    Rect filterMenu;
    struct FilterRow {
        Rect r;
        LogKind kind;
        bool heading;
        std::string label;
    };
    std::vector<FilterRow> filterRows;

    bool scrollable() const { return lineCount > linesVisible; }
    int maxOffset() const { return lineCount > linesVisible ? lineCount - linesVisible : 0; }
};

struct TipRow {
    std::string text;
    RGBA tint{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::string change;
    RGBA changeTint{ 1.0f, 1.0f, 1.0f, 1.0f };
    // A bar behind the row's text, filled this much (0 to 1; below 0: none), with a mark at barMark, amber while barLit
    float bar = -1.0f, barMark = 0.0f;
    bool barLit = false;
};

constexpr float LOD_NEAR_TILES = 96.0f;
constexpr float LOD_MID_TILES = 224.0f;
struct DetailRange {
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    float focusX = 0.0f, focusZ = 0.0f, viewRange = 0.0f;
    explicit DetailRange(const Camera& cam) : focusX(cam.focusX), focusZ(cam.focusZ), viewRange(cam.viewRange) {
        cam.eye(ex, ey, ez);
    }
    bool within(float x, float y, float z) const {
        const float dx = x - ex, dy = y - ey, dz = z - ez;
        if (dx * dx + dy * dy + dz * dz > LOD_MID_TILES * LOD_MID_TILES) return false;
        const float fx = x - focusX, fz = z - focusZ;
        return fx * fx + fz * fz <= viewRange * viewRange;
    }
};
inline bool withinDetail(const Camera& cam, float x, float y, float z) { return DetailRange(cam).within(x, y, z); }

constexpr RGBA MISSING_PROP_PINK{ 1.0f, 0.35f, 0.75f, 1.0f };
float missingPropSide(PropKind kind);

// What the world map marks. Altars, vehicles and stores each have a shape of their own; other props are small squares,
// and items lying on the ground little red dots.
enum class MapDotKind { Player, Hostile, Neutral, Prop, Altar, Vehicle, Storage, Item };
struct MapDot {
    Vec2 at{ 0.0f, 0.0f };
    MapDotKind kind = MapDotKind::Player;
};

struct WorldMapMarks {
    float camX = 0.0f, camZ = 0.0f;
    float camYaw = 0.0f;
    float moveX = 0.0f, moveZ = 0.0f;
    float moveAlpha = 0.0f;
};

struct BuildPreview {
    bool on = false;
    PropKind kind = PropKind::Wagon;
    float x = 0.0f, z = 0.0f;
    float yaw = 0.0f;   // a Perimeter piece's turn
    bool valid = false;
    float ringR = 0.0f;
    int ringTone = 0;
    std::vector<std::array<float, 3>> otherRings;   // the other claims: x, z, radius
    // A foundation being dragged out: its tiles, inclusive
    bool rect = false;
    int rx0 = 0, rz0 = 0, rx1 = 0, rz1 = 0;
    // Tiles marked at a height: tone 0 open to build on (blue), 1 will be built (green), 2 can't be (red)
    struct Mark {
        int x = 0, z = 0;
        float y = 0.0f;
        int tone = 0;
        bool outline = false;   // just the tile's edge, not filled
    };
    std::vector<Mark> marks;
    // A building blueprint being placed: an arrow on the ground in front of it, pointing the way its front faces. (cx, cz) is where the
    // arrow is middled, (dx, dz) the way it points.
    bool arrow = false;
    float arrowX = 0.0f, arrowZ = 0.0f, arrowDX = 1.0f, arrowDZ = 0.0f;
    float lift = 0.0f;   // building on an upper floor: how far above the ground the preview is drawn
};

