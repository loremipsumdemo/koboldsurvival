"""Sort the language files into the same categorized order, with # headers.

Usage: python tools/sort_language.py . [--write]   (from the repository folder)
Without --write it prints each category's keys and the ones it could not place.
"""
import re
import sys

REPO = sys.argv[1]
WRITE = '--write' in sys.argv
FILES = ['data/language/language_en.txt', 'data/language/language_zh-CN.txt']

# (header, [patterns]); a key goes to the first category with a matching pattern (regex, matched from the start)
CATEGORIES = [
    ("Language, title and loading", [r'LANGUAGE_', r'GAME_TITLE', r'LOADING_']),
    ("Unit names (lists the game picks from)", [r'NAME_']),
    ("Common buttons and words", [r'UILABEL_(YES|NO|OK|BACK|CLOSE|CONFIRM|CANCEL|CONTINUE|NEXT|DONE|ON|OFF|NONE|EMPTY|DEFAULT|CLEAR|APPLY|RESET|UNKNOWN|UNLIMITED|LIST_MORE|SCROLL|TILES|SHIFT|HOLD|CHOOSE|RENAME|REMOVE|ADD|DELETE|NO_NAME|LIST_MORE|SCROLL|SHIFT_CLICK|CHOOSE_HOW_MANY|REMOVE_ALL|NAME)(_|$)']),
    ("Title, pause, saves and scenarios", [r'SCENARIO_', r'UILABEL_(NEW|LOAD|SAVE|QUIT|RESUME|PAUSE|PAUSED|RETURN|UNSAVED|SCENARIO|PARTY|BODY|TEMPERAMENT|RANDOMIZE|IDENTITY|CUSTOM|RESTORE|SETTINGS)',
                                            r'MISC_(SAVE|LOAD|DELETE|AUTOSAVE|OVERWRITE|LEAVE|GAME|TITLE|MAIN_MAP|SCREENSHOT)']),
    ("Settings", [r'UILABEL_(GRAPHICS|INTERFACE|LANGUAGE|CONTROLS|AUDIO|DISPLAY|MONITOR|RESOLUTION|REFRESH|VSYNC|FPS|FRAME|VIEW|WINDOWED|BORDERLESS|AUTOSAVE|FONT|EDGE|CONVERSATION|GLOBAL|MUSIC|AMBIENT|SOUND|UI|CITIZEN_(SOFT|HARD)|SHOW|DEBUG|RENDER|RESET_PANELS)']),
    ("Controls list (Settings, Controls tab)", [r'CONTROL_']),
    ("World Generation, Level Editor and map pick", [r'UILABEL_(GEN|WORLD|STYLE|THEME|EDITOR|MAP|LEVEL)', r'MISC_(GEN|LEVEL|MAP)']),
    ("Panels, HUD, time and the message log", [r'UILABEL_(HUD|DATE|TIME|MONTH|DAYS|MINUTE|MINUTES|MESSAGE|INFORMATION|GAME|FILTER|LOG|UNIT|PROFILE|SEEN)', r'MISC_(LOG|FLAG)']),
    ("Squads, selection and formations", [r'UILABEL_(SQUAD|SELECT|FORM|FORMATION|PACE|OPTIMIZE|FLOOR|FOLLOW|DISBAND|MANUAL)', r'MISC_(SQUAD|SQUADS|SELECT|FORMATION|OPTIMIZE|FLOOR|ORD|DISBAND|DISBANDED)']),
    ("Unit stats, skills and needs", [r'UILABEL_(STAT|STATS|ATTRIBUTES|STRENGTH|AGILITY|DEXTERITY|VITALITY|INTELLIGENCE|WISDOM|CHARISMA|WILL|EXPERIENCE|HEALTH|HUNGER|RESTED|COMFORT|CONSCIOUS|DYING|LEVELS|COMBAT|MELEE|ARCANE|UTILITY|MEDICAL|INTERACTION|MINING|WOODCUTTING|HERBALISM|COOKING|SMELTING|FORGING|STONEWORKING|SKINNING|FARMING|CARPENTRY|LOCKPICKING|THIEVERY|CRAFTING|NEEDS|CARRY|DAMAGE|ATTACK|DEFEND|HIT|MISS|BLOCK|PROJECTILES|NPC_NAME)',
                                       r'MISC_(REASON|XP|GAINS|DIES|RECOVERS|CORPSE|EFFECT|CANT|CALMS|TURNS)']),
    ("Inventory, equipment and items", [r'UILABEL_(TIP|CLICK_SLOT|INVENTORY|EQUIPMENT|GEAR|SLOT|TYPE|SORT|ITEM|ITEMS|USE|DROP|PICKUP|PUT|TAKE|TOTAL|COUNT|INPUT|OUTPUT|LOOT|CORPSE|CRAFT)', r'FILTER_',
                                         r'MISC_(ITEM|ITEMS|GEAR|SLOT|USED|DROPS|DRINKS|PICKUP|PICKS|PUTS|LOOT|FOOD|MEAT|COOKED|CRAFT|CRAFTS|OUTPUT|EMPTY|READ|ANY|NEED)']),
    ("Behaviors, tasks and Behavior Groups", [r'UILABEL_(BEHAVIOR|TASK|TASKS|WAIT|GROUP|GROUPS|AUTO|BED|OCCUPY|RESOURCE|PASSIVE|MAN|PLACE|EVACUATE|TALK)',
                                               r'MISC_(BEHAVIOR|GROUP|GROUPS|AUTO|BED|GATHER|GATHERS|GOES|GETS|PLACES|EVACUATES|TALK|MAN|ALREADY|FREE|NOT|IS|YOU|ON|PLAYS|CLEARS|BREAKS|AIRSHIP|FLEES|FALLS|LETS)']),
    ("Building, locks and upkeep", [r'UILABEL_(BUILD|MATERIAL|CLIMB|BREAK|CANCEL_BUILD|UPKEEP|LOCK|UNLOCK)', r'MISC_(BUILD|BUILT|MATERIAL|BLUEPRINT|FOUNDATION|FLOORS|STAIRS|DOOR|DESTROY|UPKEEP|FUEL|LOCK|LOCKS|LOCKED|LOCKPICK|UNLOCKED|UNLOCKS)']),
    ("Stores and storage filters", [r'UILABEL_(STORE|STORAGE|STOCK)', r'MISC_(STORE|STORAGE)']),
    ("Altar, citizens and ghosts", [r'UILABEL_(ALTAR|GHOST|GHOSTS|APPEASE|CITIZEN|MANAGE|RENAME_UNIT)', r'MISC_(ALTAR|GHOST|GHOSTS|CITIZENS|MILITARY|HAUNTED|NO_LONGER|RENAME)']),
    ("Factions, reputation, trade, stalls and visitors", [r'FACTION_(?!MOB_)', r'NPC_', r'UILABEL_(FACTION|REP|REPUTATION|TRADE|STALL|SHOP|NPC)', r'MISC_(REPUTATION|TRADE|STALL|TRADER|SHOPPER|RAIDER|STOLEN|WANDERING)']),
    ("Props", [r'PROP_', r'MISC_PROP']),
    ("Mobs", [r'MOB_', r'FACTION_MOB_', r'UILABEL_MOB_', r'MISC_MOB_']),
    ("Item names and descriptions", [r'ITEM_\d']),
]
OTHER = "Other"

TITLE = "Title, pause, saves and scenarios"
SETTINGS = "Settings"
PANELS = "Panels, HUD, time and the message log"
SQUADS = "Squads, selection and formations"
STATS = "Unit stats, skills and needs"
INV = "Inventory, equipment and items"
BEHAVIOR = "Behaviors, tasks and Behavior Groups"
BUILD = "Building, locks and upkeep"
ALTAR = "Altar, citizens and ghosts"
TRADE = "Factions, reputation, trade, stalls and visitors"
PROPS = "Props"
MOBS = "Mobs"
COMMON = "Common buttons and words"
# Keys the patterns would put in the wrong place
OVERRIDES = {
    **dict.fromkeys(["UILABEL_TWO_HANDED", "UILABEL_PICKUP", "UILABEL_PICK_UP", "UILABEL_SINGLE", "UILABEL_MAX", "UILABEL_WILL_BE_REPLACED",
                     "UILABEL_UNKNOWN_SLOT", "MISC_NO_ROOM_INV", "MISC_CANT_BE_WORN", "MISC_NOT_FIT_SLOT", "MISC_UNKNOWN_ITEM",
                     "MISC_TWO_HANDED_IN_MAIN", "MISC_EMPTY_OFF_HAND", "MISC_EFFECT_LABEL", "UILABEL_COUNT"], INV),
    **dict.fromkeys(["UILABEL_NAME", "UILABEL_NO_SAVES", "UILABEL_HOLD_DELETE", "UILABEL_CONFIRM_STARTS", "UILABEL_ADD_UNIT",
                     "UILABEL_REMOVE_UNIT", "UILABEL_ADD_MOB", "UILABEL_NONE_AVAILABLE", "UILABEL_NEEDS_NAMES", "UILABEL_NEEDS_UNITS",
                     "UILABEL_CLICK_SAVE", "UILABEL_MODEL_N", "UILABEL_CLICK_UNIT_EDIT", "UILABEL_CLICK_TO_TYPE"], TITLE),
    **dict.fromkeys(["UILABEL_FULL_SCREEN", "UILABEL_UNIT_NAMES", "UILABEL_NPC_NAMES", "UILABEL_CRAFT_GATHER_SOUNDS"], SETTINGS),
    **dict.fromkeys(["UILABEL_COMBAT_MESSAGES", "UILABEL_EXPERIENCE_MESSAGES", "UILABEL_ITEM_MESSAGES", "UILABEL_NPC_MESSAGES",
                     "UILABEL_INTERACTION_PANEL", "UILABEL_NO_UNIT_SELECTED", "UILABEL_UNNAMED_UNIT", "MISC_YOU", "MISC_UNIT",
                     "UILABEL_TILES_N"], PANELS),
    **dict.fromkeys(["UILABEL_MODE_PASSIVE", "UILABEL_MODE_BLOCK", "UILABEL_MODE_SNEAK", "UILABEL_MODE_RANGED", "UILABEL_MODE_PASSIVE_TIP",
                     "UILABEL_MODE_BLOCK_TIP", "UILABEL_MODE_SNEAK_TIP", "UILABEL_MODE_RANGED_TIP", "MISC_IS_DOWN"], SQUADS),
    **dict.fromkeys(["UILABEL_NEXT_LEVEL_XP_LINE"], STATS),
    **dict.fromkeys(["MISC_FLOORS_COLLAPSE", "MISC_CANT_CLIMB", "MISC_ON_FLOOR"], BUILD),
    **dict.fromkeys(["UILABEL_MANAGE_UNITS", "UILABEL_RENAME_UNIT", "UILABEL_HUD_HAUNTED"], ALTAR),
    **dict.fromkeys(["UILABEL_RESET_PANELS"], SETTINGS),
    **dict.fromkeys(["UILABEL_CANCEL_BUILD"], BUILD),
    **dict.fromkeys(["MISC_ITEM_STOLEN_NAME", "MISC_ITEM_STOLEN_FROM", "MISC_FREE_TRADE_ADD", "MISC_FREE_TRADE_READY",
                     "MISC_NO_UNIT_TO_TRADE", "MISC_NEED_MORE_VALUE", "UILABEL_END_DIALOGUE", "UILABEL_TALK", "MISC_TALK_INTERRUPTED",
                     "MISC_FLAG_FACTION"], TRADE),
    **dict.fromkeys(["MISC_NO_FUEL", "MISC_NO_UPKEEP", "MISC_FUEL_LEFT", "MISC_UPKEEP_LEFT", "UILABEL_PASSIVE_HINT"], PROPS),
    **dict.fromkeys(["MISC_MATERIAL_IRON", "MISC_MATERIAL_STEEL", "MISC_MATERIAL_BRONZE", "MISC_MATERIAL_SILVER",
                     "MISC_MATERIAL_LEATHER", "MISC_MATERIAL_BONE", "MISC_MATERIAL_WOOD"], INV),
}


def parse(path):
    order, entries = [], {}
    for line in open(f'{REPO}/{path}', encoding='utf-8'):
        line = line.rstrip('\r\n')
        if not line or line.startswith('#') or '\t' not in line:
            continue
        key = line.split('\t', 1)[0].strip()
        if key in entries:
            raise SystemExit(f'duplicate {key} in {path}')
        order.append(key)
        entries[key] = line
    return order, entries


def category(key):
    if key in OVERRIDES:
        return [h for h, _ in CATEGORIES].index(OVERRIDES[key])
    for i, (_, pats) in enumerate(CATEGORIES):
        if any(re.match(p, key) for p in pats):
            return i
    return len(CATEGORIES)


en_order, _ = parse(FILES[0])
buckets = [[] for _ in range(len(CATEGORIES) + 1)]
for k in en_order:
    buckets[category(k)].append(k)
# Item names: by number, name before description
items = buckets[-2]
items.sort(key=lambda k: (int(re.match(r'ITEM_(\d+)', k).group(1)), 0 if k.endswith('_NAME') else 1, k))

if not WRITE:
    for (h, _), b in zip(CATEGORIES + [(OTHER, [])], buckets):
        print(f'== {h} ({len(b)})')
        if h != "Item names and descriptions":
            print(' '.join(b))
    sys.exit(0)

HEADER = ["# Language file. Each line is KEY, a tab, then the text shown. Keys have no spaces.",
          "# A missing key shows the English, and a key missing everywhere shows its key name.",
          "# Every language file keeps the same keys in the same order, under the same headers.",
          "# Anything the game shows the player is a key here, never English written in the code."]
for path in FILES:
    order, entries = parse(path)
    out = list(HEADER)
    placed = set()
    for (h, _), b in zip(CATEGORIES + [(OTHER, [])], buckets):
        lines = [entries[k] for k in b if k in entries]
        placed.update(k for k in b if k in entries)
        if not lines:
            continue
        out += ['', '# ' + '-' * 70, '# ' + h, '# ' + '-' * 70]
        out += lines
    extra = [k for k in order if k not in placed]
    if extra:
        out += ['', '# ' + '-' * 70, '# Only in this language (not in English)', '# ' + '-' * 70]
        out += [entries[k] for k in extra]
    assert len(placed) + len(extra) == len(order)
    open(f'{REPO}/{path}', 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
    print(path, len(order), 'keys written')
