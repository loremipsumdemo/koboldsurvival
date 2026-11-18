# MAP/SCENARIO

Scenarios made in **Add Custom Scenario** (on the title screen) are saved here as `<name>.scenario`, and New Game offers them after the
Outpost, which is built in. A scenario says how a new game on a made-up map starts: how many units, which animals, what camp and
props, and which faction rules each faction begins with. A map made from a level file places everything itself, and takes only your
party from the scenario (how many people and which animals).

A scenario is a small text file, a line a setting. A setting it does not name keeps its default: the Outpost's.

```
format 1
about A line about the scenario
player military 2
player citizens 8
player mob Wolf 1
npc military 5
npc fortified 5
npc citizens 10
npc camp 1
npc stock 100
npc standing 60
npc prop Barrel 2
npc rule trade=allies
faction 100 citizens 30
faction 114 standing -80
```

The first word of a setting line is who it is for:

- `player`: your faction, whichever color you play.
- `npc`: every faction that runs itself.
- `faction <id>`: one colored faction (100 to 115, as in `data/factions.txt`) when it is not you, over what `npc` says of it.

Then what it sets:

| line | meaning |
| --- | --- |
| `military <n>` | units that roam and fight. Yours are the ones you command: at least 1 |
| `fortified <n>` | units that guard the Perimeter (NPC factions only) |
| `citizens <n>` | units that stay at the Altar, work and build. A faction's units are these three added up. Your party holds 32 at most, with its animals |
| `caged <n>` | wild animals in cages in the camp, to tame (the Outpost's NPC factions have 3, yours none) |
| `camp <n>` | 0 none, 1 the Altar alone, 2 the whole camp. A faction that runs itself always has its Altar |
| `extras <n>` | how many props of random kinds stand about the camp (left out: three to five for NPC factions, none for yours) |
| `stock <n>` | what each stockpile of the camp starts full of: wood, stone, iron ingots, iron ore (500 by default, at most 2000) |
| `standing <n>` | how a faction that runs itself stands with you at the start, -100 to 100 (left out: what the loop of colors says, neighbors friends and the far side enemies). At -25 or below its units attack yours on sight, at 50 or above it is your ally. Not for `player` |
| `mob <key> <n>` | how many of a regular mob kind go with the faction (the key is its key in `data/mobs.txt`). Yours join your party; the others' keep near their camp |
| `prop <key> <n>` | how many of a prop stand about the camp, 5 to 12 tiles from its Altar (the key is its key in `data/props.txt`) |
| `rule <key>=<value>` | a faction rule it begins with, written as in `data/factions.txt` (`trade=allies`, `markup=25`). Yours are the rules for outsiders |

A faction that runs itself and begins with no Altar (a scenario that gives it none, or land that has no room for its camp) puts one
down first of all: at the nearest place with a block of 10 by 10 tiles of free ground within 100 tiles of where its units would be,
else on the nearest free ground tile.
