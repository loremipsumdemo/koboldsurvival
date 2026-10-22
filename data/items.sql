-- Item database schema
-- Columns, in order:
--   id                 : unique item ID number
--   type_id            : item category ('item', 'consumable', 'weapon', 'armor', 'accessory')
--   max_stack_size     : max quantity per inventory slot
--   name               : item display name
--   description        : flavor/text description string
--   max_durability     : maximum durability (0 = not applicable, e.g. consumables)
--   current_durability : current durability
--   weight             : item weight
--   buy_value          : price to buy from vendor
--   sell_value         : price when sold to vendor
--   required_level     : minimum level to use/equip
--   attack_value       : attack stat (weapons only, 0 otherwise)
--   defense_value      : defense stat (armor only, 0 otherwise)

CREATE TABLE items (
    id                 INTEGER PRIMARY KEY,
    type_id            TEXT    NOT NULL CHECK (type_id IN ('item', 'consumable', 'weapon', 'armor', 'accessory')),
    max_stack_size     INTEGER NOT NULL DEFAULT 1,
    name               TEXT    NOT NULL,
    description        TEXT,
    max_durability     INTEGER NOT NULL DEFAULT 0,
    current_durability INTEGER NOT NULL DEFAULT 0,
    weight             REAL    NOT NULL DEFAULT 0,
    buy_value          INTEGER NOT NULL DEFAULT 0,
    sell_value         INTEGER NOT NULL DEFAULT 0,
    required_level     INTEGER NOT NULL DEFAULT 1,
    attack_value       INTEGER NOT NULL DEFAULT 0,  -- weapons only
    defense_value      INTEGER NOT NULL DEFAULT 0   -- armor only
);

-- Starting item set (values follow the column order above):
--   (id, type_id, max_stack_size, name, description, max_durability, current_durability, weight, buy_value, sell_value, required_level, attack_value, defense_value)
INSERT INTO items (id, type_id, max_stack_size, name, description, max_durability, current_durability, weight, buy_value, sell_value, required_level, attack_value, defense_value) VALUES
-- ===== ITEMS (materials, quest, misc) =====
(1,  'item',      99, 'Iron Ore',           'Raw ore that can be smelted into iron ingots.',          0,   0, 1.0,  10,  5, 1, 0, 0),
(2,  'item',      99, 'Oak Log',            'A sturdy log used in crafting and construction.',        0,   0, 2.0,   8,  4, 1, 0, 0),
(3,  'item',      99, 'Healing Herb',       'A common herb with mild restorative properties.',        0,   0, 0.2,   5,  2, 1, 0, 0),
(4,  'item',     200, 'Gold Coin',          'Standard currency of the realm.',                        0,   0, 0.0,   1,  0, 1, 0, 0),
(5,  'item',       1, 'Wolf Pelt',          'A rough pelt taken from a slain wolf. Sought by tanners.',0,   0, 1.5,  20, 12, 1, 0, 0),
(6,  'item',       1, 'Ancient Relic',      'A mysterious artifact humming with faint energy.',       0,   0, 0.5,   0, 250, 10, 0, 0),

-- ===== CONSUMABLES =====
(7,  'consumable', 20, 'Health Potion',     'Restores 50 HP when consumed.',                          0,   0, 0.5,  25, 12, 1, 0, 0),
(8,  'consumable', 20, 'Mana Potion',       'Restores 50 MP when consumed.',                          0,   0, 0.5,  30, 15, 1, 0, 0),
(9,  'consumable', 10, 'Stamina Draught',   'Restores 40 stamina and briefly boosts regeneration.',   0,   0, 0.4,  20, 10, 1, 0, 0),
(10, 'consumable',  5, 'Elixir of Might',   'Grants +10 attack for 60 seconds.',                      0,   0, 0.3,  75, 35, 5, 0, 0),
(11, 'consumable', 10, 'Bread',             'Simple travel food. Restores 15 HP.',                    0,   0, 0.3,   4,  2, 1, 0, 0),
(12, 'consumable',  5, 'Antidote',          'Cures poison and reduces poison damage for 30 seconds.', 0,   0, 0.2,  18,  9, 1, 0, 0),

-- ===== WEAPONS =====
(13, 'weapon',      1, 'Rusty Dagger',      'A worn dagger. Better than bare fists, barely.',        30,  30, 1.0,  15,  7, 1,  4, 0),
(14, 'weapon',      1, 'Iron Sword',        'A sturdy blade forged from iron.',                     100, 100, 5.0, 120, 60, 5, 15, 0),
(15, 'weapon',      1, 'Short Bow',         'A compact bow favored by hunters and scouts.',          80,  80, 2.5, 100, 50, 4, 12, 0),
(16, 'weapon',      1, 'Oak Staff',         'A carved staff that channels arcane energy.',           70,  70, 3.0,  95, 47, 4, 10, 0),
(17, 'weapon',      1, 'Steel Greatsword',  'A heavy two-handed blade with devastating reach.',     160, 160, 9.0, 350,175, 10, 32, 0),
(18, 'weapon',      1, 'Hunting Knife',     'A small blade useful for skinning and close defense.',   45,  45, 0.8,  22, 11, 1,  5, 0),

-- ===== ARMOR =====
(19, 'armor',       1, 'Cloth Robe',        'Simple robes offering little more than modesty.',        40,  40, 1.5,  20, 10, 1, 0,  3),
(20, 'armor',       1, 'Leather Chestpiece','Basic hide armor offering light protection.',            80,  80, 4.0,  90, 45, 3, 0, 10),
(21, 'armor',       1, 'Iron Cuirass',      'Solid iron plating that turns aside weak blows.',       130, 130,12.0, 200,100, 6, 0, 22),
(22, 'armor',       1, 'Steel Helm',        'A forged helm that protects the skull in battle.',      100, 100, 3.5, 140, 70, 5, 0, 12),
(23, 'armor',       1, 'Leather Boots',     'Comfortable boots for long marches.',                    50,  50, 1.5,  40, 20, 1, 0,  4),
(24, 'armor',       1, 'Tower Shield',      'A massive shield that absorbs heavy impacts.',          180, 180,15.0, 260,130, 8, 0, 28),

-- ===== ACCESSORIES =====
(25, 'accessory',   1, 'Copper Ring',       'A plain copper band. Slightly boosts vitality.',          0,   0, 0.1,  35, 17, 1, 0,  2),
(26, 'accessory',   1, 'Amulet of Focus',   'A gem amulet that sharpens the wearer''s spellcraft.',    0,   0, 0.2, 120, 60, 5,  3, 0),
(27, 'accessory',   1, 'Cloak of Shadows',  'A dark cloak that muffles movement and presence.',        0,   0, 1.0, 150, 75, 7,  0,  5),
(28, 'accessory',   1, 'Lucky Charm',       'A small trinket said to bring good fortune.',             0,   0, 0.1, 200,100, 4,  0,  0),
(29, 'accessory',   1, 'Band of Vigor',     'A silver ring that bolsters stamina and recovery.',       0,   0, 0.1, 180, 90, 6,  2,  3);