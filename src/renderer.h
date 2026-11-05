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
#include "ui_layout.h"
#include "sky.h"
#include "combat.h"
#include "components.h"
#include "ecs.h"
#include "mobs.h"
#include "model.h"
#include "terrain.h"
#include "terrain_mesh.h"
#include "ui_types.h"
#include "party_ui.h"
#include "props.h"
#include "overview.h"
#include "ui_icons.h"

// A texture plus its width/height ratio
struct TextTex {
    GLuint tex = 0;
    float aspect = 1.0f;
};

// Multiplies the texture color, alpha included.
struct RGBA {
    float r, g, b, a;
};

// The look of the menus and panels, after the logo: deep maroon-black, coral pink and warm parchment gold, with cream
// and dusty taupe text. (PINK is the coral accent; GREEN, by its old name, is now the gold highlight.)
namespace theme {
inline constexpr RGBA PINK{ 0.910f, 0.376f, 0.431f, 1.0f };        // #e8606e, coral
inline constexpr RGBA PINK_DEEP{ 0.588f, 0.196f, 0.259f, 1.0f };   // #963242
inline constexpr RGBA PINK_DARK{ 0.298f, 0.086f, 0.125f, 1.0f };   // #4c1620
inline constexpr RGBA PINK_INK{ 0.098f, 0.027f, 0.039f, 1.0f };    // #19070a
inline constexpr RGBA GREEN{ 0.961f, 0.788f, 0.459f, 1.0f };       // #f5c975, gold
inline constexpr RGBA GREEN_DEEP{ 0.353f, 0.235f, 0.110f, 1.0f };  // #5a3c1c, the lit button's fill
inline constexpr RGBA GREEN_DARK{ 0.157f, 0.098f, 0.039f, 1.0f };  // #28190a
inline constexpr RGBA BONE{ 0.957f, 0.902f, 0.796f, 1.0f };        // #f4e6cb, cream text
inline constexpr RGBA ASH{ 0.643f, 0.553f, 0.510f, 1.0f };         // #a48d82, quiet text
inline constexpr RGBA NOTICE_TEXT{ 0.871f, 0.776f, 0.690f, 1.0f }; // #dec6b0
inline constexpr RGBA NIGHT{ 0.082f, 0.039f, 0.047f, 1.0f };       // #150a0c, panels
inline constexpr RGBA alpha(RGBA c, float a) { return RGBA{ c.r, c.g, c.b, a }; }
} // namespace theme

// A corner of a flat shape drawn with drawUiTris, in window pixels, with its own colour
struct UiVert {
    float x, y;
    RGBA c;
};

// One line of the hover list in the top-left corner: what is under the mouse, in the colour for its sort
struct HudEntry {
    std::string text;
    RGBA color;
};
inline constexpr RGBA HUD_UNIT_COLOR{ 1.0f, 1.0f, 1.0f, 1.0f };    // unit names: white
inline constexpr RGBA HUD_ITEM_COLOR{ 1.0f, 0.72f, 0.2f, 1.0f };   // items on the ground: orange-yellow
inline constexpr RGBA HUD_PROP_COLOR{ 0.68f, 0.68f, 0.68f, 1.0f }; // props: gray

// The Information Panel's Time tab: the time of day, the date written out and as numbers, and the days since the start
struct InfoTime {
    std::string clock;     // "14:05"
    std::string date;      // "DAY 12 · THIRD MONTH · YEAR 500"
    std::string ymd;       // "500-03-12"
    std::string days;      // "Days passed: 71"
};

// The Information Panel, in the top left corner by default. The newest line is at the bottom, and offset counts
// the lines scrolled up from it. Lines are drawn bottom-aligned, so the first line sits in the last row.
struct GameLogLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect bar{ 0.0f, 0.0f, 0.0f, 0.0f };     // the "Information Panel" bar across the top
    Rect track{ 0.0f, 0.0f, 0.0f, 0.0f };   // scroll bar track
    Rect thumb{ 0.0f, 0.0f, 0.0f, 0.0f };   // scroll bar thumb
    float textX = 0.0f, textW = 0.0f;
    float lineH = 0.0f;
    int linesVisible = 0;
    int lineCount = 0;
    int offset = 0;                          // lines scrolled up from the newest, within range
    int first = 0;                           // index of the oldest visible line
    int shown = 0;                           // how many lines are visible

    bool collapsed = false;                  // only the bar shows
    Rect toggle{ 0.0f, 0.0f, 0.0f, 0.0f };  // the [-] or [+] button in the bar
    Rect lock{ 0.0f, 0.0f, 0.0f, 0.0f };    // the lock beside it
    float bodyTop = 0.0f, bodyBottom = 0.0f;   // the panel less its bar, which runs along its top or its bottom
    // The three tabs under the bar: Message (0), Game (1) and Time (2). On the Game tab, the Filter button and its menu.
    int tab = 0;
    Rect tabMessage{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect tabGame{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect tabTime{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect filterBtn{ 0.0f, 0.0f, 0.0f, 0.0f };
    bool filterOpen = false;
    Rect filterMenu{ 0.0f, 0.0f, 0.0f, 0.0f };
    struct FilterRow {
        Rect r;
        LogKind kind;
        bool heading;
        std::string label;
    };
    std::vector<FilterRow> filterRows;       // the menu's rows, when it is open

    bool scrollable() const { return lineCount > linesVisible; }
    int maxOffset() const { return lineCount > linesVisible ? lineCount - linesVisible : 0; }
};

// One line of an item tooltip: its text, and optionally a change against the item it is compared with,
// such as "(+3)" in green when better or red when worse
struct TipRow {
    std::string text;
    RGBA tint{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::string change;
    RGBA changeTint{ 1.0f, 1.0f, 1.0f, 1.0f };
};

// Terrain is drawn at full detail within LOD_NEAR_TILES of the eye, at half detail out to LOD_MID_TILES, and at a
// quarter beyond. Units, items, their names and labels, and damage numbers are only drawn, and only picked, within
// LOD_MID_TILES: beyond it they are not loaded onto the screen.
constexpr float LOD_NEAR_TILES = 96.0f;
constexpr float LOD_MID_TILES = 224.0f;
inline bool withinDetail(const Camera& cam, float x, float y, float z) {
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);
    const float dx = x - ex, dy = y - ey, dz = z - ez;
    if (dx * dx + dy * dy + dz * dz > LOD_MID_TILES * LOD_MID_TILES) return false;
    // And within the view range (the Graphics setting) of the camera's focus, measured across the ground
    const float fx = x - cam.focusX, fz = z - cam.focusZ;
    return fx * fx + fz * fz <= cam.viewRange * cam.viewRange;
}

// The stand-in for a prop whose model is not in data.pak: a pink square, missingPropSide(kind) tiles on a side
constexpr RGBA MISSING_PROP_PINK{ 1.0f, 0.35f, 0.75f, 1.0f };
float missingPropSide(PropKind kind);

// A mark on the world map: a unit's place and whose it is, or a prop (any kind) as a purple square
enum class MapDotKind { Player, Hostile, Neutral, Prop };
struct MapDot {
    Vec2 at{ 0.0f, 0.0f };
    MapDotKind kind = MapDotKind::Player;
};

// What the world map shows besides the dots: the camera's pivot and the way it faces, and the green X where the last
// move order went (faded out by its alpha)
struct WorldMapMarks {
    float camX = 0.0f, camZ = 0.0f;
    float camYaw = 0.0f;          // the camera's yaw: it looks along -sin(yaw), -cos(yaw) on the ground
    float moveX = 0.0f, moveZ = 0.0f;
    float moveAlpha = 0.0f;       // 0 hides the X
};

// The placement preview of a prop picked on the Build tab: its footprint drawn on the ground as a red grid, green where
// it can be placed (valid) and red where it can't. Drawn over drawFrame while a prop is picked and the mouse is on the
// ground. kind is the PropKind; (x, z) its centre; yaw its facing.
struct BuildPreview {
    bool on = false;
    PropKind kind = PropKind::Wagon;
    float x = 0.0f, z = 0.0f, yaw = 0.0f;
    bool valid = false;
    // A claim circle drawn around (x, z) with this radius, when ringR is above 0. ringTone: 1 green (a flag that can be
    // placed), 2 red (one that can't), 3 white (an existing flag under the mouse). It shows without a footprint too.
    float ringR = 0.0f;
    int ringTone = 0;
    // Every other flag in the world, by its spot: while any claim circle shows, all of these show theirs too (white),
    // so the claims around can be seen at once. The flag the main circle belongs to is left out.
    std::vector<std::pair<float, float>> otherRings;
};

class Renderer {
public:
    // Creates the GL programs, then loads every image and model. progress (0..1) is
    // called between steps so the loading screen can be drawn while this runs.
    // font: large font for the symbol fallbacks. hudFont: small font for the hover label.
    // uiFont: font for menu buttons and headings.
    bool init(TTF_Font* font, TTF_Font* hudFont, TTF_Font* uiFont,
              const std::function<void(float, const std::string&)>& progress = {});
    void shutdown();

    // In-game time of day in minutes, 0..1440. Sets the sun position and sky colors.
    void setDayMinutes(float m) { dayMinutes_ = m; }
    // Sun, moon, star and sky color settings
    void setSky(const SkyTuning& s) { sky_ = s; }

    // Sets up the terrain's chunks for a map, replacing any earlier one. Each chunk's mesh is built
    // when it is first drawn, at the detail its distance from the camera needs.
    void uploadTerrain(const Terrain& t);

    // Loading screen: black, with a progress bar (progress 0..1) along the bottom 4%
    // category: what kind of asset is loading now, named under the bar. backdrop: the title screen's moving backdrop
    // behind it; without it (the first load, before the game has started) the screen is plain dark.
    void drawLoadingScreen(float progress, int winW, int winH, const std::string& label = std::string(),
                           const std::string& category = std::string(), bool backdrop = true);

    // Units whose sprite covers the pixel (mx, my), in entity order
    std::vector<Entity> unitsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH);
    // Props whose model covers the pixel (mx, my), in entity order. The test uses the model's box on screen.
    std::vector<Entity> propsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH);
    // Items on the ground whose sprite covers the pixel (mx, my), in entity order
    std::vector<Entity> groundItemsAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my,
                                           int winW, int winH);

    // hud: the hover list drawn in the top-left corner, one coloured line each (empty for none)
    void drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                   const std::vector<HudEntry>& hud, int winW, int winH);

    // Tile-sized ring on the ground under the camera's pivot point, drawn over drawFrame.
    // alpha 0..1 fades it. Shown while the middle mouse button orbits the camera.
    void drawPivotRing(const Camera& cam, const Terrain& t, float alpha, int winW, int winH);

    // The Build tab's state, set each frame before drawing: the prop picked (-1 for none), and its placement preview
    void setBuildSelected(const BuildPick& pick) { buildSelected_ = pick; }
    // Whether a selected unit of yours is in one of your claims: when not, only the kinds buildAnywhere allows are lit
    void setBuildInClaim(bool in) { buildInClaim_ = in; }
    void setBuildPreview(const BuildPreview& p) { buildPreview_ = p; }
    // The footprint preview on the ground (called by drawFrame)
    void drawBuildPreview(const Terrain& t);
    // The Build tab's body: the heading (what is picked), then one row a kind, with its select button and name
    void drawBuildSection(const SidebarLayout& L, float mx, float my);

    // Crystal marking where a right-click order was given, standing on the ground at (x, z).
    // alpha 0..1 fades it.
    void drawActionCrystal(const Camera& cam, const Terrain& t, float x, float z, float alpha, int winW, int winH);

    // In-game panels, drawn between beginUi and endUi. The bottom panel shows the walk and run toggle
    // (running says which is on), then each unit with its portrait, a health bar and its name, and a purple
    // frame around the selected ones. A portrait goes red from the bottom as its unit loses health.
    // The Faction Panel: its tabs, then the Squad tab's units or the Reputation tab's list for the faction repView
    void drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<Entity>& units, bool running,
                       int repView = FACTION_PLAYER);
    // The world map window: its title bar with the X that closes it, the level's ground `span` tiles from top to
    // bottom centred on cx, cz (sea past the edge), the props as purple squares, the units as dots, the camera's pivot
    // as a ring with a triangle pointing the way it faces, and a green X where the last move order went
    void drawWorldMap(const WorldMapLayout& L, const std::vector<MapDot>& dots, float cx, float cz, float span,
                      const WorldMapMarks& marks);
    // The talk box: a unit's name and what it says, with a Close button
    // The talk box: the last lines said with their portraits, the options, and End Dialogue
    void drawDialogue(const DialogueLayout& L, const std::vector<SpokenLine>& convo,
                      const std::vector<DialogueOption>& options, float mx, float my);
    // The text split into lines no wider than maxW, at spaces
    std::vector<std::string> wrapListText(const std::string& text, float maxW);
    // Where the hover names in the top left start, from the top of the window
    void setHudTop(float y) { hudTop_ = y; }
    // Right sidebar for one unit: its profile card, its gear, its 32 inventory slots and its behavior rules. (mx, my)
    // is the mouse, for hover. dimSlot is an inventory slot, and dimGear a gear slot, whose item is being
    // dragged out, or -1 for none. dragItem is the item id being dragged (0 for none): gear slots then
    // show green or red for whether it may go there. editable says whether the sort and the behavior edits work; for
    // a red unit they are drawn dimmed.
    // The Stats tab's rows; returns the stat under the mouse, or -1
    // A mob's (mob) lists only its attributes and combat skills
    int drawStatsTab(const SidebarLayout& L, const Stats& stats, float mx, float my, bool mob = false);
    void drawStatTip(const Stats& stats, StatId stat, float mx, float my);
    void drawInventorySidebar(const SidebarLayout& L, World& w, Entity unit, float mx, float my,
                              int dimSlot, int dimGear, int dragItem, bool editable);
    // The full text of the behavior row under the mouse, beside it. Call after drawInventorySidebar.
    void drawBehaviorTip(const SidebarLayout& L, World& w, Entity unit, float mx, float my);
    // Clips drawing to r until endClip, in window pixels with the top left at the top left. The sidebar's
    // content scrolls under its panel, so it is clipped to the panel.
    void beginClip(const Rect& r);
    void endClip();
    // The stack being dragged, drawn under the mouse
    void drawDraggedStack(float mx, float my, int item, int count);
    // One item in a cell: its picture, and its count when more than one
    void drawItemCell(const Rect& r, int item, int count);
    // The PAUSED bar across the top of the window, and text drawn scaled up about its centre
    void drawPauseBar(bool hint);
    // The pause bar's height, with or without its hint line, so what must not hide under it can start below it
    float pauseBarHeight(bool hint);
    // Trade value lines over their units
    void drawTradeLabels(const std::vector<TradeLabel>& labels, World& w, const Camera& cam, int winW, int winH);
    void drawUiTextLarge(const std::string& s, float cx, float cy, float scale, RGBA tint);
    // The trade popout: the NPC's inventory, and both sides of the trade as ghosts. dragFrom is the NPC slot being
    // dragged, shown as not there, or -1.
    // The storage panel for a storage or crafting prop: its name, its items (the one being dragged, dragFrom, left
    // out), a hint and Close. A hovered item shows its tooltip.
    // The storage panel. output is a passive crafting prop's output, or null; its cells are panel slots STORAGE_SLOTS
    // and up, as is dragFrom when it is one of them. status is the forge's fuel line, empty for other props.
    // The looting panel over an Unconscious unit or a corpse: its gear and its inventory, each item taken with a click
    void drawLoot(const LootLayout& L, const std::string& name, const Equipment* worn, const Inventory* inv, float mx, float my);
    void drawStorage(const StorageLayout& L, const std::string& propName, const std::string& hint,
                     const Storage* storage, const Output* output, const std::string& status, int dragFrom,
                     float mx, float my, const std::string& toggleLabel = std::string());
    // The emotes over each unit's head (emotesOf in systems.h), above its name when names are shown. A unit with
    // several shows one at a time, changing every EMOTE_CYCLE_MS.
    void drawEmotes(World& w, const Camera& cam, int winW, int winH, bool playerNames, bool otherNames);
    // Emotes over props: No upkeep over a vault with no logs in it, Decaying over a flag wearing down. Above the prop's
    // name when prop names are shown.
    void drawPropEmotes(World& w, const Camera& cam, int winW, int winH, bool propNames);
    // A hint over the unit under the pointer of what a right-click does to it (EmoteId::CuePutDown, EmoteId::CueTalk)
    void drawHoverCue(World& w, const Camera& cam, int winW, int winH, Entity u, EmoteId emote, bool namesShown);
    // A progress ring over each resource or crafting prop being worked: a dark brown ring that fills with orange-yellow,
    // clockwise from the top, as its next item nears. Each time an item is finished the ring hops up and back down once.
    // The work rings over worked props; with names set they sit above the props' name tags
    void drawPropProgress(World& w, const Camera& cam, int winW, int winH, bool names = false);
    // yourName: your trading unit's name, over your side. showValue: whether to draw the value bar, which a trade
    // with an NPC has and a free trade between your own units does not.
    void drawTrade(const TradeLayout& L, const std::string& npcName, const std::string& yourName, const Inventory* npcInv,
                   const std::vector<TradeGhost>& theirs, const std::vector<TradeGhost>& yours, int dragFrom,
                   const TradeStatus& status, bool showValue, float mx, float my);
    // The quantity prompt over the trade: a count with -100 -10 - + +10 +100, Single, Max, Confirm and Cancel
    // The Optimize All dialog: what it will do for unitCount units, the Attack and Defense slider at attackShare
    // (0 to 100), the three toggles (accessories, two-handed weapons, value on ties), and Confirm and Cancel
    void drawOptimizeAll(const OptimizeAllLayout& L, int unitCount, int attackShare, const std::array<bool, 3>& toggles,
                         float mx, float my);
    void drawQuantityPrompt(const QuantityLayout& L, const std::string& itemName, int chosen, int available,
                            float mx, float my);
    // The Disband popout for a unit of yours: its portrait, name and a few lines about it, what disbanding it does, and
    // Disband (filled by held, 0 to 1, while it is held down) and Cancel
    void drawDisband(World& w, const DisbandLayout& L, Entity unit, float held, float mx, float my);
    // A notice, drawn centerd at the top of the window
    void drawNotice(const std::string& text, int winW, int winH);
    // The stats popout for one unit, and its layout (which depends on the fonts)
    StatsPanelLayout statsPanelLayout(int winW, int winH);
    void drawStatsPanel(const StatsPanelLayout& L, const std::string& name, const Stats& stats, float mx, float my);
    // The right-click menu of an inventory item, opened at (x, y), and its drawing
    ItemMenuLayout itemMenuLayout(float x, float y, int buttonCount, int winW, int winH);
    void drawItemMenu(const ItemMenuLayout& L, const std::vector<std::string>& labels, float mx, float my);

    // Items lying on the ground, drawn as billboards over loaded ground (called by drawFrame). Each shows
    // its picture from data/item (item_<n>.png, else item_0.png), or the pink square when there is neither.
    // While Alt is held, drawGroundLabels draws a name box over each one in view (between beginUi
    // and endUi), and groundItemAtPixel finds the item under the mouse, label or sprite. The name
    // line on top under (mx, my) is drawn highlighted.
    void drawGroundLabels(World& w, const Terrain& t, const Camera& cam, int winW, int winH, float mx, float my);
    // labels: also the name lines of the Alt boxes (only while they are shown); the sprite's visible part always counts
    Entity groundItemAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my, int winW, int winH,
                             bool labels = true);
    // An item's name in a box over its sprite on the ground ("3x Iron Ore" for a stack), for the item under the pointer
    void drawGroundItemName(World& w, const Camera& cam, int winW, int winH, Entity item);

    // Each unit's name over its head, in brackets with a drop shadow. Hostile units are shown in red.
    // Name tags over the units: yours when players is set, every other unit's when others is set
    void drawUnitNames(World& w, const Camera& cam, int winW, int winH, bool players, bool others);
    // Each prop's name over it, in brackets and purple, like a unit's tag
    // showInfo: each name gets its prop's info lines under it (owner, health, decay, lock; see propInfoLines)
    void drawPropNames(World& w, const Camera& cam, int winW, int winH, bool showInfo = false);

    // Damage numbers over the units they hit, rising and fading from the combat system's list. They draw
    // between beginUi and endUi, after the names.
    void drawFloatingNumbers(const std::vector<FloatingNumber>& floats, const Camera& cam, int winW, int winH);
    // The game log (the Information Panel). offset is how far it is scrolled up, and rightLimit is the x its
    // panel must stay left of, so it clears the unit panel. The result has offset clamped to its range.
    GameLogLayout gameLogLayout(int winW, int winH, int tab, bool filterOpen, int offset, int lineCount,
                                    bool collapsed = false, const PanelPlace& place = PanelPlace{});
    // The Information Panel: rows are the lines of the tab shown, oldest first. The Time tab shows `time` instead.
    void drawGameLog(const GameLogLayout& L, const std::vector<const LogLine*>& rows, const LogFilter& filter,
                       float mx, float my, const InfoTime& time = InfoTime{});

    // The starting party menu. The buttons come from layoutPartySetup, and this draws them with the
    // party's pictures, names and totals. Call between beginUi and endUi.
    void drawPartySetup(const std::vector<Button>& buttons, const Party& p, const PartyUi& ui,
                        const PartyPanels& P, float mx, float my);
    // A unit's picture, fitted inside r and kept in its proportions
    void drawUnitPicture(int sprite, const Rect& r);
    // A unit's model filling r, standing and turned by yaw about its vertical axis, seen from the front; its picture
    // when it has no model. Call between beginUi and endUi.
    void drawUnitModelPreview(int sprite, const Rect& r, float yaw);
    // A mob kind seen the same way: its model, or its picture where it has none
    void drawMobPreview(MobKind kind, const Rect& r, float yaw);
    // A model, turned by yaw, standing in a box of the UI with its own depth
    void drawModelPreview(const Model* m, const Rect& r, float yaw);
    // A mob kind's picture (mob_<kind>.png), the stand-in for a missing one, or the pink square for a kind with a model
    const TextTex& mobPicture(MobKind kind) const;

    // Menu drawing. Call beginUi before the first menu draw and endUi after the last.
    void beginUi(int winW, int winH);
    void endUi();
    // Clears, then draws the menus' moving backdrop: a deep maroon night, a jagged band of parchment gold and one of coral
    // drifting up each side, a veil, a vignette and scan lines. Behind the title screen and every menu.
    void drawTitleBackground();
    // The title screen's header: the game's name, large, in the top left, and the pink notice board under it with
    // the lines of data/motd.txt. A line starting "# " is a heading, "- " a bullet point, and an empty line a gap.
    // Lines that don't fit the board are left off.
    // The title screen's top left: the "Early Access" line, the game's name (first word green, a pink bar, the second
    // word bone-white, in capitals), the pink rule, and the notice board with the lines of data/motd.txt
    void drawTitleHeader(const TitleLayout& L, const std::string& first, const std::string& second,
                         const std::vector<std::string>& news);
    // A title or pause menu item: capitals, ash-gray on a dark band with a gray edge on the left; selected, bone-white
    // on a green band with a green edge, moved a little left. sub is a second, smaller line (the language in use).
    // appear runs 0 to 1 as the item slides in when the screen opens.
    void drawMenuItem(const Rect& r, const std::string& label, const std::string& sub, bool selected, bool enabled,
                      float appear);
    // The line along the bottom of the title screen: left text on the left; the arrow keys, then right text, on the right
    void drawTitleFooter(const Rect& footer, const std::string& left, const std::string& right);
    // A menu panel: a pink frame with its bottom right corner cut off, over a near-black maroon fill
    void drawThemedPanel(const Rect& r);
    // Flat triangles in window pixels, three corners each, each corner its own colour (blended between)
    void drawUiTris(const std::vector<UiVert>& v);
    // A rectangle shaded from left to right
    void drawRectGradientH(const Rect& r, RGBA left, RGBA right);
    // Hover sounds. Buttons, menu items and tabs note the one under the pointer as they are drawn. The game calls
    // hoverFrameStart before drawing a frame and hoverFrameEnd after: true when the pointer (or the arrow keys) has
    // just come onto a different one.
    void noteHover(const Rect& r);
    // The font the game's name is drawn in on the title screen: a very large one, so the name is drawn shrunk, never
    // stretched, and stays sharp. The renderer does not own it. Setting it drops the name's old pictures.
    void setTitleFont(TTF_Font* font);
    void hoverFrameStart() { hoverAny_ = false; }
    bool hoverFrameEnd();
    void drawRect(const Rect& r, RGBA c);
    // A rectangle with round corners, in window pixels
    void drawRoundedRect(const Rect& r, float radius, RGBA c);
    // A UI icon in r: its picture from data/icon (icon_<n>.png, else icon_0.png), or the pink placeholder with its id
    void drawUiIcon(int icon, const Rect& r);
    // An icon of height h at (x, y), with its text beside it in the list font
    void drawIconText(float x, float y, float h, int icon, const std::string& text, RGBA tint);
    // An icon and text, centred together in r. uiFont picks the menu font, else the list font.
    void drawIconLabel(const Rect& r, int icon, const std::string& text, RGBA tint, bool uiFont);
    // A bar across the top of a panel: the icon and the title in it
    void drawTitleBar(const Rect& bar, int icon, const std::string& title);
    // The [-] when the panel is open, the [+] when it is folded
    void drawCollapseButton(const Rect& r, bool collapsed);
    // The X that closes a window, at the right end of its title bar
    void drawCloseButton(const Rect& r);
    // The Faction Panel's Reputation tab (drawUnitPanel)
    void drawReputationList(const UnitPanelLayout& L, int repView);
    // The lock beside a panel's [-] or [+]: closed while locked, open and green while the panel can be moved and resized
    void drawLockButton(const Rect& r, bool locked);
    void drawButton(const Rect& r, const std::string& label, bool enabled, bool hovered);
    void drawUiText(const std::string& s, float x, float y, RGBA tint);
    void drawUiTextCentered(const std::string& s, float cx, float y, RGBA tint);
    // Text that does not fit its box is drawn smaller, down to a floor, then shortened. The full text shows in a
    // tooltip while the mouse is over the box: noteTip sets it, and endUi draws it.
    void setMouse(float x, float y) { mouseX_ = x; mouseY_ = y; }
    void noteTip(const std::string& full, const Rect& area);
    void drawUiTextInBox(const std::string& s, const Rect& box, RGBA tint);
    void drawUiTextScaled(const std::string& s, float x, float y, float k, RGBA tint);
    std::string fitShrinkText(const std::string& s, float boxW);
    float fitShrinkScale(const std::string& shown, float boxW, float boxH);
    float uiTextWidth(const std::string& s);
    float uiTextHeight(const std::string& s);
    // The height the Unit Panel's profile card needs for its text at the current fonts, in a window H pixels high:
    // the name, the faction, the body type, then health, hunger and carry with their bars
    float profileCardHeight(int H);

    // Smaller text for dense lists, such as the save list. The font is owned by the caller.
    void setListFont(TTF_Font* font);
    // Fonts for the Stats popout: NotoSans Regular, or Bold if the Regular file is not packed. Owned by the caller.
    void setStatsFonts(TTF_Font* title, TTF_Font* rows);
    void setFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow);
    // The menus' text: the same five fonts at a larger size (MENU_FONT_SCALE), for the title, New Game, the scenarios,
    // the party menu, Settings and the Esc menu. setMenuText picks which set every text call uses until it is called
    // again; each set keeps its own text pictures, so switching is cheap. The fonts are owned by the caller.
    void setMenuFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow);
    void setMenuText(bool on);
    bool menuText() const { return menuText_; }
    // Text drawn from the very large title font and shrunk to height h, centered on (cx, cy), so large text stays sharp.
    // Without that font, the UI font enlarged.
    void drawBigText(const std::string& text, float cx, float cy, float h, RGBA tint);

    void drawListText(const std::string& s, float x, float y, RGBA tint);
    float listTextHeight(const std::string& s);
    float listTextWidth(const std::string& s);
    // A small button with list-font text, for the rows of the save list
    void drawListButton(const Rect& r, const std::string& label, bool hovered);
    // A button whose background fills from left to right (fill 0..1), for the hold-to-delete button
    void drawHoldButton(const Rect& r, const std::string& label, float fill);

private:
    // Text line with its texture and size in pixels
    struct HudLine {
        std::string text;
        TextTex tex;
        float w = 0.0f, h = 0.0f;
    };

    // Text cut to fit maxW at a word boundary, with "..." added. ui picks the menu font, else the list font.
    std::string fitText(const std::string& text, float maxW, bool ui);
    // The behavior list: its heading, the modes, column headings and one row per behavior
    // task: what the unit is doing (taskNameOf in systems.h), shown in the header as "Task: ..."
    // modes: Passive, Block, Sneak and Ranged, lit while on. Ranged is set by the weapon, so it is never pressed.
    void drawBehaviorSection(const SidebarLayout& L, const std::vector<BehaviorRule>& rules, const std::string& task,
                             float mx, float my, bool editable, const std::array<bool, 4>& modes = {});
public:
    // The Behavior tab's # prompt (layoutPercentPrompt): title says what is asked, typed is the digits so far, range
    // the percents it takes, and error why the last try was refused (empty when none)
    void drawPercentPrompt(const PercentPromptLayout& P, const std::string& title, const std::string& typed,
                           const std::string& range, const std::string& error, float mx, float my);

private:
    // A small list-font button with a centered label, for the sort, priority and delete buttons
    void drawSmallButton(const Rect& r, const std::string& label, bool enabled, bool hovered);
    // A quad that changes color from top to bottom
    void drawRectGradient(const Rect& r, RGBA top, RGBA bottom);
    // Clock text: every digit takes the width of the widest digit, so the numbers don't shift
    float digitCellWidth();
    float clockTextWidth(const std::string& s);
    void drawClockText(const std::string& s, float x, float y, RGBA tint);
    // The name and stats of an item, beside the mouse. count is shown as a stack. When compareTo is given
    // and the item is gear, its stats show how they change from the gear it would replace in compareTo,
    // and a card beside it shows the gear it is compared with.
    void drawItemTooltip(int item, int count, float mx, float my, const Equipment* compareTo = nullptr);
    // A tooltip box with its rows, measured and drawn with its top-left corner at (x, y)
    float tipWidth(const std::vector<TipRow>& rows);
    float tipHeight(size_t rowCount);
    void drawTip(const std::vector<TipRow>& rows, float x, float y, float width);

    // Draws a textured quad: 4 vertices of (x, y, z, u, v), in the order BL, BR, TR, TL
    void drawQuadVerts(GLuint tex, const float* v, RGBA tint);
    // A textured UI quad from (x0, y0) top left to (x1, y1) bottom right, with texture coordinates u0 v0 at
    // the top left and u1 v1 at the bottom right
    void drawUiQuad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                    RGBA tint);
    // A green dot with a black outline, d pixels across, centred on (cx, cy)
    void drawDot(float cx, float cy, float d);
    // Takes the overview picture if its build has finished, and uploads it
    void pollOverview();
    OverviewBuilder overview_;
    // The UI icon pictures, by id. An id with no picture has a texture of 0.
    const TextTex& uiIconTexture(int icon);
    std::map<int, TextTex> uiIcons_;
    GLuint overviewTex_ = 0;
    float overviewSize_ = 0.0f;       // tiles per side of the level, 0 while there is no picture
    float overviewExtent_ = 1.0f;     // tiles the picture covers per side
    GLuint dotTex_ = 0;
    GLuint dotWhiteTex_ = 0;   // a white dot, tinted for the red and neutral units
    float hudTop_ = 12.0f;

    // The picture for an item, from data/item/item_<n>.png. An item without one uses data/item/item_0.png, and the pink
    // square without that. Never null.
    const TextTex* itemTexture(int item);
    // Which pixels of an item's picture can be seen, for picking a ground item by its visible part only. Loaded once
    // per item from its picture (item_0.png, else a solid square, standing in when it has none).
    struct ItemMask {
        int w = 0, h = 0;
        std::vector<unsigned char> alpha;
    };
    std::map<int, ItemMask> itemMasks_;
    const ItemMask& itemMask(int item);
    static ItemMask loadMask(const std::string& path);
    // Whether the picture of an item is visible at (u, v), each 0 to 1 across and down
    bool itemPixelSolid(int item, float u, float v);
    // An item's picture at exactly ITEM_PICTURE_PX square (ui_types.h), on whole pixels: centered in a cell, or on a point
    void drawItemPicture(const Rect& cell, int item, float alpha);
    void drawItemPictureAt(float cx, float cy, int item, float alpha);
    // A picture fitted inside r, keeping its proportions, multiplied by tint
    void drawFitted(const TextTex& g, const Rect& r, RGBA tint);

    // Symbol-style billboard that stands on (x, y, z) and turns to face a point 200 units behind the camera
    void drawGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint);
    // A sprite lying on its side (an Unconscious unit), h long, moved `toward` the camera
    void drawLyingGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint, float toward);
    // How colorful the glyph program draws: 1 as the picture is, 0 fully gray. Set back to 1 after use.
    void setSaturation(float sat);
    float unitTop(World& w, Entity u);
    // How far above its feet a unit's sprite or model is drawn: a flying mob's hover (mobHoverFraction, mobs.h) while it
    // is up and about, else 0
    float unitHover(World& w, Entity u) const;
    // A unit lying in a bed or bedroll: how far above its feet it is drawn (half the bed's height, from the bed's base),
    // or below 0 when it is not in one. It counts only while the unit is still on the bed's spot and not carried, so a
    // unit that somehow left the bed with its Occupying still on is drawn standing, never stuck tilted or raised.
    float bedLift(World& w, Entity u) const;

    // 3D model standing on (x, y, z), scaled so its largest side is targetSize
    void drawModel(const Model& m, float x, float y, float z, float targetSize, float yaw = 0.0f);

    // Text lines in the top-left corner, each on a dark panel, each in its own colour
    void drawHud(const std::vector<HudEntry>& lines, int winW, int winH);

    // The sprite for a unit, or the fallback symbol if its image is missing
    const TextTex& unitGlyph(World& w, Entity e) const;

    // Ground items on loaded chunks, as pink sprites
    void drawGroundItems(World& w, const Terrain& t, const Camera& cam);

    // One name box over the items on one tile. Each item has a row, top row first, and the row is
    // the part of the box that picks that item. dist2 is the squared distance from the camera to the
    // nearest item on the tile.
    struct GroundStack {
        Rect box;
        std::vector<Entity> items;
        std::vector<std::string> names;
        std::vector<Rect> rows;
        float dist2 = 0.0f;
    };
    // The name boxes for the items on loaded, visible ground, farthest first, so a nearer box is drawn
    // over the ones behind it. Drawing and clicking both use this.
    std::vector<GroundStack> groundStacks(World& w, const Terrain& t, const Camera& cam, int winW, int winH);
    // The name line on top under (mx, my), as an index into stacks and its row. A box hides the boxes
    // behind it, so a line under a nearer box is not found. Returns false when there is none.
    static bool topLabelRow(const std::vector<GroundStack>& stacks, float mx, float my, size_t& stack, size_t& row);

    // Cached white text texture for a menu string, created on first use
    HudLine& uiLine(const std::string& s);

    // Cached white text texture for a list string, using the list font
    HudLine& listLine(const std::string& s);
    // A font with its own cache of text textures, for the Stats popout
    struct TextFont {
        TTF_Font* font = nullptr;
        std::map<std::string, HudLine> lines;
    };
    HudLine& fontLine(TextFont& f, const std::string& s);
    void drawFontText(TextFont& f, const std::string& s, float x, float y, RGBA tint);
    float fontTextWidth(TextFont& f, const std::string& s);
    float fontTextHeight(TextFont& f, const std::string& s);

    GLuint colorProgram_ = 0, glyphProgram_ = 0;
    GLint uMVPColor_ = -1, uMVPGlyph_ = -1, uTint_ = -1, uSat_ = -1, uFaction_ = -1;

    GLuint dynVao_ = 0, dynVbo_ = 0;          // color overlays
    BuildPick buildSelected_;                 // what the Build tab has picked
    bool buildInClaim_ = false;               // a selected unit of yours stands in one of your claims
    BuildPreview buildPreview_;               // its placement preview this frame
    GLuint glyphVao_ = 0, glyphVbo_ = 0;      // one textured quad at a time
    // One chunk of the terrain on the GPU. Its buffers are reused when its detail changes.
    // One slot per chunk of the level, indexed by chunk id. Buffers exist only while the chunk is loaded.
    struct TerrainChunk {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei indexCount = 0;
        int step = 0;              // detail step the buffers hold, 0 until first built
        int nbStep[4] = { 0, 0, 0, 0 };   // west, east, north, south neighbors' steps the buffers were built with
        uint32_t version = 0;      // version of the chunk the buffers and bounds were made from
        ChunkBounds bounds{};
    };
    std::vector<TerrainChunk> chunks_;
    int terrainSize_ = 0;
    int chunksPerSide_ = 0;
    ChunkMesh chunkScratch_;       // reused while building chunks
    void releaseTerrain();
    void releaseChunk(TerrainChunk& c);
    void buildChunk(const Terrain& t, const ChunkData& c, int id, int step, const int* nbStep);
    void drawTerrain(const Terrain& t, const Camera& cam);

    // Terrain, sea and seabed shaders and textures (renderer_terrain.cpp)
    struct TerrainShader {
        GLuint prog = 0;
        GLint viewProj = -1, eye = -1, seaY = -1, depthScale = -1, rockRgb = -1, rockSlope = -1, sunDir = -1;
        GLint sunLight = -1, ambient = -1, underwater = -1, fogColor = -1, fog = -1, tile = -1;
    };
    struct SeaShader {
        GLuint prog = 0;
        GLint viewProj = -1, center = -1, half = -1, eye = -1, sunDir = -1, sunLight = -1, ambient = -1, color = -1;
        GLint fogColor = -1, fog = -1, time = -1, opacity = -1, pattern = -1, speed = -1, fadeStart = -1, fadeEnd = -1, tile = -1;
    };
    struct FloorShader {
        GLuint prog = 0;
        GLint viewProj = -1, center = -1, half = -1, eye = -1, ambient = -1, sunLight = -1, underwater = -1, fogColor = -1, fog = -1;
    };
    TerrainShader groundShader_;
    SeaShader seaShader_;
    FloorShader floorShader_;
    GLuint groundTex_ = 0, seaTex_ = 0;      // the surface tiles of the world being drawn
    uint32_t surfaceVersion_ = 0;            // which world's surface those tiles came from
    GLuint emptyVao_ = 0;                    // for shaders that build their quads from gl_VertexID
    float skySun_[3] = { 0.0f, 1.0f, 0.0f }; // the sun the sky draws, which lights the terrain
    bool initTerrainGl();
    void releaseTerrainGl();
    void uploadSurfaceTiles(const Terrain& t);

    TTF_Font* titleFont_ = nullptr;
    std::map<std::string, TextTex> titleWords_;   // the name's words, drawn once in titleFont_, with mipmaps
    // A word of the name as a picture: its texture, and width over height
    const TextTex& titleWord(const std::string& text);
    TextTex titleLogo_;           // data/illustration/title.png: the game's logo, drawn in place of the name's words
    TextTex loadingArt_;          // data/illustration/loading<1-3>.png, one picked at random: the start-up loading picture
    bool hoverAny_ = false;       // something was noted under the pointer this frame
    Rect hoverNow_{ 0.0f, 0.0f, 0.0f, 0.0f }, hoverLast_{ 0.0f, 0.0f, 0.0f, 0.0f };
    bool hoverHad_ = false;       // something was under the pointer last frame
    float titleMouse_ = 0.0f;     // the mouse's place across the window, -1 to 1, eased, for the backdrop's sway
    unsigned long long titleMouseAt_ = 0;   // when it was last eased, in milliseconds
    std::vector<TextTex> mobSprites_;   // data/mob/mob_<kind>.png, kind 1 first
    // data/profile/profile_<nn>.png: the portrait of a unit drawn by model nn, so a unit's picture always goes with its
    // model. A missing one is the pink square.
    std::array<TextTex, UNIT_SPRITE_COUNT> profilePics_{};
    // A unit's portrait for the squad panel and its Profile tab: its model's profile picture, or its mob kind's picture
    const TextTex& unitPortrait(World& w, Entity e) const;
    TextTex mobFallback_;
    // A unit's flat picture, for the portraits: the pink square for every unit until units have 2D art again
    const TextTex& unitPicture(int n) const;
    // One model per unit sprite number, data/model/unit/unit_<nn>.glb. Files with the same bytes share one copy:
    // unitModelFor_[n - 1] points at the model sprite n draws, or is null when its file is missing or unreadable.
    std::array<Model, UNIT_SPRITE_COUNT> unitModels_{};
    std::array<const Model*, UNIT_SPRITE_COUNT> unitModelFor_{};
    // One model per kind of mob that has one (MobInfo::model): data/model/mob/mob_<nn>.glb. Empty for the rest.
    std::array<Model, MOB_KIND_COUNT> mobModels_{};
    // How tall a unit stands as drawn: its mob kind's model height, or GLYPH_H
    float unitHeight(World& w, Entity e) const;
    // The model for sprite number n (wrapped into range), or null
    const Model* unitModelNumber(int n) const;
    // A unit's model, or null for a mob (drawn by its sprite) or a unit whose model is missing
    const Model* unitModel(World& w, Entity e) const;
    // A unit's width over its height as drawn standing: its model's, or its sprite's
    float unitAspect(World& w, Entity e) const;
    // A unit model standing on (x, y, z) GLYPH_H tall, facing yaw (0 faces +z); lying on its side when lying, on top
    // of y, moved `toward` the camera
    void drawUnitModel(const Model& m, float x, float y, float z, float yaw, bool lying, float toward, bool carried = false,
                       float height = 1.2f);
    // The color a unit's model is tinted toward: its faction's, or none (alpha 0) for your own units and mobs
    RGBA unitFactionTint(World& w, Entity e) const;
    // Which way each unit's model faces: turned toward the way it last walked. One idle for UNIT_SETTLE_MS is settled:
    // it keeps its facing, however units close by nudge it, until it stops being idle or moves more than
    // UNIT_SETTLE_SLIP tiles from where it settled.
    struct UnitFacing {
        float x = 0.0f, z = 0.0f;              // where it was last frame
        float yaw = 0.0f;
        unsigned long long idleSince = 0;      // when it went idle, in milliseconds; 0 while it is not idle
        bool settled = false;
        float settleX = 0.0f, settleZ = 0.0f;  // where it settled
    };
    std::map<Entity, UnitFacing> unitFacing_;
    float unitYaw(Entity e, float x, float z, bool idle);
    // Item pictures by item id, loaded on first use. An id with no file holds an empty texture.
    std::map<int, TextTex> itemPics_;
    std::map<int, TextTex> emotePics_;   // data/emote/emote_<n>.png, loaded the first time each is shown
    const TextTex* emoteTexture(EmoteId emote);   // never null: emote_0.png or the pink square stands in
    TextTex itemNone_;        // data/item/item_0.png, for items without a picture of their own
    TextTex emoteFallback_;   // data/emote/emote_0.png, for emotes without a picture of their own
    TextTex iconFallback_;    // data/icon/icon_0.png, for UI icons without a picture of their own
    TextTex pink_;            // one pink texel: the square drawn for a picture missing along with its _0 stand-in
    // A plain white texel. Items are drawn as this, tinted pink, until they have art.
    TextTex white_;
    // Each worked prop's count of finished items as last drawn, and when its ring last hopped (seconds), so the ring
    // can hop once for each item finished
    struct PropHop {
        unsigned made = 0;
        float start = -1.0f;
    };
    std::map<Entity, PropHop> propHops_;
    // A flat ring, or a part of one, in window pixels: the part from `from` to `to` (0 to 1, clockwise from the top)
    void drawRing(float cx, float cy, float outer, float inner, float from, float to, RGBA color);
    // One model per prop kind (props.h). A kind whose model failed to load has an empty model, and propModel gives
    // the stand-in, model_0.glb, in its place (also empty if that failed, and the kind is drawn as a pink square).
    std::array<Model, PROP_KIND_COUNT> propModels_{};
    Model fallbackModel_{};
    const Model& propModel(PropKind kind) const;
    // How tall a prop of this kind stands, in world units: its model's height as drawn, or the pink stand-in's
    float propTop(PropKind kind) const;
    // The height a prop stands at: its own spot on the ground, or the top of the foundation it sits on (OnBase), so a
    // vault on a foundation is drawn on top of it rather than sunk into it
    float propBaseY(World& w, Entity e) const;
    // The top of each prop's block of name lines, as drawPropNames last drew it, so the progress ring sits above it
    std::map<Entity, float> propLabelTop_;

    // Hover label: one cached texture per line, rebuilt only when its text changes
    TTF_Font* font_ = nullptr;
    TTF_Font* hudFont_ = nullptr;
    std::vector<HudLine> hudLines_;

    // Menus
    TTF_Font* uiFont_ = nullptr;
    std::map<std::string, HudLine> uiText_;

    // Smaller font for dense lists
    TTF_Font* listFont_ = nullptr;
    std::map<std::string, HudLine> listText_;
    TextFont statsTitle_, statsRow_;
    // The text set not in use (the menus' while the game's is, and the other way round), swapped in by setMenuText
    struct TextSet {
        TTF_Font* hud = nullptr;
        std::vector<HudLine> hudLines;
        TTF_Font* ui = nullptr;
        std::map<std::string, HudLine> uiText;
        TTF_Font* list = nullptr;
        std::map<std::string, HudLine> listText;
        TextFont statsTitle, statsRow;
    };
    TextSet parked_;
    bool menuText_ = false;
    void swapTextSet();
    void replaceActiveFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow);
    void clearActiveText();
    Mat4 pixels_{};
    int uiW_ = 0, uiH_ = 0;
    float mouseX_ = 0.0f, mouseY_ = 0.0f;   // the mouse, for tooltips and for hover sounds in panels drawn without it
    std::string tip_;                        // the full text to show as a tooltip, or empty
    void drawTip();

    Mat4 viewProj_{};

    // Sky: a gradient and sun drawn as a full-screen quad, colored by the time of day
    GLuint skyProgram_ = 0, skyVao_ = 0, skyVbo_ = 0;
    GLint uSkyInvVP_ = -1, uSunDir_ = -1, uSunAlpha_ = -1, uZenith_ = -1, uHorizon_ = -1, uSunCol_ = -1;
    GLint uMoonDir_ = -1, uMoonAlpha_ = -1, uMoonCol_ = -1, uStarAlpha_ = -1;
    float dayMinutes_ = 360.0f;
    SkyTuning sky_;
    // The point sprites turn to face: 200 units behind the camera
    float billX_ = 0.0f, billZ_ = 0.0f;
};
