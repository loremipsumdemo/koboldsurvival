# MAP/BLUEPRINT

Plans made in **Building Blueprints** (on the title screen) are saved here as `<name>.blueprint`.
A plan is a small text file: the size of its box and each piece's prop key, where it stands in the box (in tiles), which floor
it is on, its turn, and how it was adjusted. It holds no model and no material: the game's `data/props.txt` gives each piece its
model and its cost, and what a building is made of is chosen when it is built.

A faction other than yours that has an Altar builds from these plans inside its claim:

- It needs a plan with a foundation of at least 3 by 3 and a door on the ground floor. Building Blueprints will not save one that is
  missing either. Any shape and any walls are fine; only a complete building (every edge tile of the ground floor walled, with a
  door) adds to the claim.
- It has up to 6 buildings in its claim: 3 of wood, and once those 3 are complete, 2 of stone and 1 of metal. One is built at a
  time, and a new one stands at least a tile from any other building.
- The material for the plan has to be in its stores. Its citizens fetch it and build, as yours do with Auto-Construct.
- A plan is turned any of four ways to fit. Keep it no bigger than about 40 tiles across, so that it fits an Altar's claim.
- Every complete building adds 2 to the radius of the Altar's claim, if no other Altar's claim is in the way.
