## Controls

| Input | Action |
| --- | --- |
| Left click / drag | Select units. Shift adds to the selection. A box around ground items, and no units you haven't selected, picks the items up |
| Right click on your Altar | Manage Units (move units between the military and the citizens), Behavior Groups or Appease Ghosts, with or without units selected |
| Optimize / right-click | Optimize the gear of the squad shown; right-click for Selected, All or Custom |
| Numpad 1 to 9 | Select that squad. Shift adds it to the selection, and pressing it twice brings the camera to it |
| Ctrl + Numpad 1 to 9 | Move the selected units into that squad |
| Numpad 0 | Select your unit nearest the camera's centre |
| Right click | Orders and context menus (move, use a prop, talk, break bones or crystals, put down) |
| Right mouse held | Selected units walk straight at the pointer |
| Shift with moving a stack | Choose how many to trade or store. Without Shift, the whole stack moves |
| WASD / arrow keys | Move the camera. Shift moves it faster |
| Middle mouse drag | Orbit the camera |
| Mouse wheel | Zoom, or scroll lists |
| Alt (held) | Show the names of items on the ground |
| Form button | List the formations; click one to use it. Changing the shape or spacing keeps the formation's place and facing |
| Alt + 1 / 2 / 3 / 4 | Selected units form a line, circle, rectangle or wedge, and later orders use that shape |
| Alt + 5 | Switch between tight and loose formations |
| Alt + right mouse drag | Place the selected units around where the drag starts; how far you drag sets the size (a circle's radius, half a line's length), looking away from where they are now |
| Space | Pause or resume, also during a conversation |
| M | Open or close the world map |
| Escape | Cancel, back out of menus, open the game menu |
| Floor button, Q / E | The floor shown, built on and clicked on. Auto follows the unit the camera follows; Manual steps with Q (down) and E (up). Inside a building the floors above are cut away |
| R (placing Stairs) | Turn the stairs: they rise toward the way they face, and you step off onto the floor above past their top |
| F12 | Take a screenshot |

## World Generation and the Level Editor

**World Generation** on the title screen makes new maps: pick a map style, tune the land, hills, mountains, moisture,
temperature and forests, set the look on the Theme tab, preview and generate. **Continue to Level Editor** places the
spawn point, units, mobs, props and items (Tab switches between the editor's own UI and the game's).

Maps are saved in [MAP](MAP) and levels in [MAP/LEVEL](MAP/LEVEL); New Game lets you choose the map and level to start on.

## Text and languages

All text the game shows comes from the language files in [data/language](data/language): `language_en.txt` (English,
the base) and `language_zh-CN.txt` (Simplified Chinese). Each line is a key, a tab, and the text; `{0}`, `{1}` mark
values filled in by the game.

When you add anything the player can read, add a key instead of writing English in the code: button and menu labels,
tooltips, messages, prop, item and mob names, and default text such as the starting world name. Put the English in
`language_en.txt` and the translation in every other language file, then show it with `lang("KEY")` or
`langf("KEY", { ... })`.

Every language file keeps the same keys in the same order, under the same `#` category headers (Settings, Building,
Props and so on). Put a new key under the header it belongs to, or add it anywhere and run
`python tools/sort_language.py . --write`, which files every key under its category in all the language files. Without
`--write` it lists each category's keys, and any it can't place go under Other.
