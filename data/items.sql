-- Item database schema
-- Columns, in order:
--   id                 : unique item ID number
--   type_id            : item category ('item', 'consumable', 'weapon', 'armor', 'accessory')
--   max_stack_size     : max quantity per inventory slot
--   name               : base name, e.g. 'Short Sword'
--   description        : flavor/text description string
--   max_durability     : maximum durability (0 = not applicable, e.g. consumables)
--   current_durability : current durability
--   weight             : item weight
--   buy_value          : price to buy from vendor
--   sell_value         : price when sold to vendor
--   required_level     : minimum level to use/equip (gear: the tier)
--   attack_value       : attack stat (weapons only, 0 otherwise)
--   defense_value      : defense stat (armour and shields, 0 otherwise)
--   on_use             : what right-clicking does from the inventory, written as a short script:
--                        statements of health or hunger with a sign and a number, separated by ';'
--                        e.g. health +50; hunger +10; attack +10 for 60s. Empty for none.
--   material           : gear material, e.g. 'Iron'. Empty for items with no material.
--   tier               : gear tier, 1 or more. Shown after the name as +N, so
--                        material 'Iron', name 'Short Sword', tier 4 is 'Iron Short Sword +4'. 0 for none.
--   equip_slot         : where gear goes. Empty for items that are not gear. Values:
--                        head, torso, legs, feet, accessory (either of two),
--                        main_hand, off_hand, either_hand (main or off hand),
--                        two_hand (main hand, and the off hand must be empty)

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
    defense_value      INTEGER NOT NULL DEFAULT 0,  -- armour and shields only
    on_use             TEXT,                        -- right-click script, NULL for none
    material           TEXT,                        -- gear only, NULL for none
    tier               INTEGER NOT NULL DEFAULT 0,  -- gear only, 0 for none
    equip_slot         TEXT                         -- gear only, NULL for none
);

-- Item rows. Ids 1-12 are the original items, 13-22 are food, 23 on are gear.
INSERT INTO items (id, type_id, max_stack_size, name, description, max_durability, current_durability, weight, buy_value, sell_value, required_level, attack_value, defense_value, on_use, material, tier, equip_slot) VALUES
(1, 'item', 99, 'Iron Ore', 'Raw ore that can be smelted into iron ingots.', 0, 0, 1.0, 10, 5, 1, 0, 0, NULL, NULL, 0, NULL),
(2, 'item', 99, 'Oak Log', 'A sturdy log used in crafting and construction.', 0, 0, 2.0, 8, 4, 1, 0, 0, NULL, NULL, 0, NULL),
(3, 'item', 99, 'Healing Herb', 'A common herb with mild restorative properties.', 0, 0, 0.2, 5, 2, 1, 0, 0, NULL, NULL, 0, NULL),
(4, 'item', 200, 'Gold Coin', 'Standard currency of the realm.', 0, 0, 0.0, 1, 0, 1, 0, 0, NULL, NULL, 0, NULL),
(5, 'item', 1, 'Wolf Pelt', 'A rough pelt taken from a slain wolf. Sought by tanners.', 0, 0, 1.5, 20, 12, 1, 0, 0, NULL, NULL, 0, NULL),
(6, 'item', 1, 'Ancient Relic', 'A mysterious artifact humming with faint energy.', 0, 0, 0.5, 0, 250, 10, 0, 0, NULL, NULL, 0, NULL),
(7, 'consumable', 20, 'Health Potion', 'Restores 50 HP when consumed.', 0, 0, 0.5, 25, 12, 1, 0, 0, 'health +50', NULL, 0, NULL),
(8, 'consumable', 20, 'Mana Potion', 'Restores 50 MP when consumed.', 0, 0, 0.5, 30, 15, 1, 0, 0, NULL, NULL, 0, NULL),
(9, 'consumable', 10, 'Stamina Draught', 'Restores 40 stamina and briefly boosts regeneration.', 0, 0, 0.4, 20, 10, 1, 0, 0, NULL, NULL, 0, NULL),
(10, 'consumable', 5, 'Elixir of Might', 'Grants +10 attack for 60 seconds.', 0, 0, 0.3, 75, 35, 5, 0, 0, 'attack +10 for 60s', NULL, 0, NULL),
(11, 'consumable', 10, 'Bread', 'Simple travel food. Restores 15 HP.', 0, 0, 0.3, 4, 2, 1, 0, 0, 'hunger +30; health +15', NULL, 0, NULL),
(12, 'consumable', 5, 'Antidote', 'Cures poison and reduces poison damage for 30 seconds.', 0, 0, 0.2, 18, 9, 1, 0, 0, NULL, NULL, 0, NULL),
(13, 'consumable', 20, 'Apple', 'Restores 8 hunger when eaten.', 0, 0, 0.3, 8, 4, 1, 0, 0, 'hunger +8', NULL, 0, NULL),
(14, 'consumable', 20, 'Wild Berries', 'Restores 6 hunger when eaten.', 0, 0, 0.3, 6, 3, 1, 0, 0, 'hunger +6', NULL, 0, NULL),
(15, 'consumable', 20, 'Hard Biscuit', 'Restores 12 hunger when eaten.', 0, 0, 0.3, 12, 6, 1, 0, 0, 'hunger +12', NULL, 0, NULL),
(16, 'consumable', 20, 'Cheese Wedge', 'Restores 15 hunger when eaten.', 0, 0, 0.3, 15, 7, 1, 0, 0, 'hunger +15', NULL, 0, NULL),
(17, 'consumable', 20, 'Smoked Fish', 'Restores 20 hunger when eaten.', 0, 0, 0.4, 20, 10, 1, 0, 0, 'hunger +20', NULL, 0, NULL),
(18, 'consumable', 20, 'Honey Cake', 'Restores 18 hunger when eaten.', 0, 0, 0.4, 18, 9, 1, 0, 0, 'hunger +18', NULL, 0, NULL),
(19, 'consumable', 20, 'Mutton Pie', 'Restores 26 hunger when eaten.', 0, 0, 0.5, 26, 13, 1, 0, 0, 'hunger +26', NULL, 0, NULL),
(20, 'consumable', 20, 'Stew Bowl', 'Restores 30 hunger when eaten.', 0, 0, 0.5, 30, 15, 1, 0, 0, 'hunger +30', NULL, 0, NULL),
(21, 'consumable', 20, 'Roast Boar Leg', 'Restores 40 hunger when eaten.', 0, 0, 0.6, 40, 20, 1, 0, 0, 'hunger +40', NULL, 0, NULL),
(22, 'consumable', 20, 'Trail Ration', 'Restores 50 hunger when eaten.', 0, 0, 0.7, 50, 25, 1, 0, 0, 'hunger +50', NULL, 0, NULL),
(23, 'armor', 1, 'Cap', 'Protects the head.', 48, 48, 1.4, 13, 6, 1, 0, 3, NULL, 'Leather', 1, 'head'),
(24, 'armor', 1, 'Hood', 'Protects the head.', 54, 54, 1.4, 13, 6, 2, 0, 3, NULL, 'Leather', 2, 'head'),
(25, 'armor', 1, 'Coif', 'Protects the head.', 89, 89, 2.4, 50, 25, 3, 0, 6, NULL, 'Iron', 3, 'head'),
(26, 'armor', 1, 'Helm', 'Protects the head.', 67, 67, 2.0, 26, 13, 2, 0, 4, NULL, 'Bronze', 2, 'head'),
(27, 'armor', 1, 'Visor', 'Protects the head.', 122, 122, 2.2, 101, 50, 4, 0, 8, NULL, 'Steel', 4, 'head'),
(28, 'armor', 1, 'Bascinet', 'Protects the head.', 98, 98, 2.4, 57, 28, 4, 0, 7, NULL, 'Iron', 4, 'head'),
(29, 'armor', 1, 'Sallet', 'Protects the head.', 133, 133, 2.2, 112, 56, 5, 0, 9, NULL, 'Steel', 5, 'head'),
(30, 'armor', 1, 'Great Helm', 'Protects the head.', 107, 107, 2.4, 57, 28, 5, 0, 7, NULL, 'Iron', 5, 'head'),
(31, 'armor', 1, 'Circlet', 'Protects the head.', 67, 67, 1.8, 108, 54, 3, 0, 6, NULL, 'Silver', 3, 'head'),
(32, 'armor', 1, 'Crown', 'Protects the head.', 86, 86, 1.8, 123, 61, 6, 0, 7, NULL, 'Silver', 6, 'head'),
(33, 'armor', 1, 'Tunic', 'Covers the chest and body.', 48, 48, 4.2, 22, 11, 1, 0, 6, NULL, 'Leather', 1, 'torso'),
(34, 'armor', 1, 'Jerkin', 'Covers the chest and body.', 54, 54, 4.2, 25, 12, 2, 0, 7, NULL, 'Leather', 2, 'torso'),
(35, 'armor', 1, 'Lamellar Vest', 'Covers the chest and body.', 54, 54, 4.8, 29, 14, 2, 0, 7, NULL, 'Bone', 2, 'torso'),
(36, 'armor', 1, 'Brigandine', 'Covers the chest and body.', 74, 74, 6.0, 56, 28, 3, 0, 10, NULL, 'Bronze', 3, 'torso'),
(37, 'armor', 1, 'Hauberk', 'Covers the chest and body.', 98, 98, 7.2, 99, 49, 4, 0, 13, NULL, 'Iron', 4, 'torso'),
(38, 'armor', 1, 'Breastplate', 'Covers the chest and body.', 82, 82, 6.0, 61, 30, 4, 0, 11, NULL, 'Bronze', 4, 'torso'),
(39, 'armor', 1, 'Cuirass', 'Covers the chest and body.', 107, 107, 7.2, 106, 53, 5, 0, 14, NULL, 'Iron', 5, 'torso'),
(40, 'armor', 1, 'Chestplate', 'Covers the chest and body.', 133, 133, 6.6, 211, 105, 5, 0, 18, NULL, 'Steel', 5, 'torso'),
(41, 'armor', 1, 'Full Plate', 'Covers the chest and body.', 144, 144, 6.6, 222, 111, 6, 0, 19, NULL, 'Steel', 6, 'torso'),
(42, 'armor', 1, 'Ceremonial Plate', 'Covers the chest and body.', 86, 86, 5.4, 228, 114, 6, 0, 14, NULL, 'Silver', 6, 'torso'),
(43, 'armor', 1, 'Breeches', 'Covers the legs.', 48, 48, 2.4, 16, 8, 1, 0, 4, NULL, 'Leather', 1, 'legs'),
(44, 'armor', 1, 'Trousers', 'Covers the legs.', 54, 54, 2.4, 16, 8, 2, 0, 4, NULL, 'Leather', 2, 'legs'),
(45, 'armor', 1, 'Leggings', 'Covers the legs.', 54, 54, 2.8, 22, 11, 2, 0, 5, NULL, 'Bone', 2, 'legs'),
(46, 'armor', 1, 'Chausses', 'Covers the legs.', 74, 74, 3.5, 36, 18, 3, 0, 6, NULL, 'Bronze', 3, 'legs'),
(47, 'armor', 1, 'Tassets', 'Covers the legs.', 82, 82, 3.5, 41, 20, 4, 0, 7, NULL, 'Bronze', 4, 'legs'),
(48, 'armor', 1, 'Cuisses', 'Covers the legs.', 98, 98, 4.2, 71, 35, 4, 0, 9, NULL, 'Iron', 4, 'legs'),
(49, 'armor', 1, 'Greaves', 'Covers the legs.', 107, 107, 4.2, 71, 35, 5, 0, 9, NULL, 'Iron', 5, 'legs'),
(50, 'armor', 1, 'Plate Leggings', 'Covers the legs.', 133, 133, 3.9, 145, 72, 5, 0, 12, NULL, 'Steel', 5, 'legs'),
(51, 'armor', 1, 'Mail Leggings', 'Covers the legs.', 89, 89, 4.2, 64, 32, 3, 0, 8, NULL, 'Iron', 3, 'legs'),
(52, 'armor', 1, 'Gilded Greaves', 'Covers the legs.', 86, 86, 3.1, 168, 84, 6, 0, 10, NULL, 'Silver', 6, 'legs'),
(53, 'armor', 1, 'Sandals', 'Protects the feet.', 48, 48, 1.0, 10, 5, 1, 0, 2, NULL, 'Leather', 1, 'feet'),
(54, 'armor', 1, 'Shoes', 'Protects the feet.', 54, 54, 1.0, 10, 5, 2, 0, 2, NULL, 'Leather', 2, 'feet'),
(55, 'armor', 1, 'Boots', 'Protects the feet.', 60, 60, 1.0, 10, 5, 3, 0, 2, NULL, 'Leather', 3, 'feet'),
(56, 'armor', 1, 'Riding Boots', 'Protects the feet.', 65, 65, 1.0, 13, 6, 4, 0, 3, NULL, 'Leather', 4, 'feet'),
(57, 'armor', 1, 'Hobnail Boots', 'Protects the feet.', 74, 74, 1.5, 21, 10, 3, 0, 3, NULL, 'Bronze', 3, 'feet'),
(58, 'armor', 1, 'Wrappings', 'Protects the feet.', 54, 54, 1.2, 11, 5, 2, 0, 2, NULL, 'Bone', 2, 'feet'),
(59, 'armor', 1, 'Marching Boots', 'Protects the feet.', 89, 89, 1.8, 36, 18, 3, 0, 4, NULL, 'Iron', 3, 'feet'),
(60, 'armor', 1, 'Sabatons', 'Protects the feet.', 98, 98, 1.8, 36, 18, 4, 0, 4, NULL, 'Iron', 4, 'feet'),
(61, 'armor', 1, 'Sollerets', 'Protects the feet.', 133, 133, 1.7, 79, 39, 5, 0, 6, NULL, 'Steel', 5, 'feet'),
(62, 'armor', 1, 'Warboots', 'Protects the feet.', 144, 144, 1.7, 79, 39, 6, 0, 6, NULL, 'Steel', 6, 'feet'),
(63, 'accessory', 1, 'Band', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.2, 11, 5, 1, 0, 1, NULL, 'Bronze', 1, 'accessory'),
(64, 'accessory', 1, 'Charm', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.2, 8, 4, 2, 0, 1, NULL, 'Bone', 2, 'accessory'),
(65, 'accessory', 1, 'Bracelet', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.2, 22, 11, 3, 0, 2, NULL, 'Iron', 3, 'accessory'),
(66, 'accessory', 1, 'Brooch', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.2, 16, 8, 3, 0, 2, NULL, 'Bronze', 3, 'accessory'),
(67, 'accessory', 1, 'Ring', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.2, 48, 24, 2, 0, 2, NULL, 'Silver', 2, 'accessory'),
(68, 'accessory', 1, 'Amulet', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.2, 48, 24, 3, 0, 2, NULL, 'Silver', 3, 'accessory'),
(69, 'accessory', 1, 'Pendant', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.2, 48, 24, 4, 0, 2, NULL, 'Silver', 4, 'accessory'),
(70, 'accessory', 1, 'Talisman', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.2, 8, 4, 4, 0, 1, NULL, 'Bone', 4, 'accessory'),
(71, 'accessory', 1, 'Torc', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.2, 46, 23, 5, 0, 3, NULL, 'Steel', 5, 'accessory'),
(72, 'accessory', 1, 'Cloak', 'A small worn trinket. Two can be worn at once.', 0, 0, 0.1, 10, 5, 6, 0, 2, NULL, 'Leather', 6, 'accessory'),
(73, 'weapon', 1, 'Short Sword', 'Held in the main hand.', 89, 89, 2.4, 92, 46, 3, 10, 0, NULL, 'Iron', 3, 'main_hand'),
(74, 'weapon', 1, 'Long Sword', 'Held in the main hand.', 122, 122, 3.3, 224, 112, 4, 16, 0, NULL, 'Steel', 4, 'main_hand'),
(75, 'weapon', 1, 'Axe', 'Held in the main hand.', 81, 81, 3.8, 84, 42, 2, 9, 0, NULL, 'Iron', 2, 'main_hand'),
(76, 'weapon', 1, 'Mace', 'Held in the main hand.', 74, 74, 3.5, 54, 27, 3, 8, 0, NULL, 'Bronze', 3, 'main_hand'),
(77, 'weapon', 1, 'Flail', 'Held in the main hand.', 98, 98, 3.6, 101, 50, 4, 11, 0, NULL, 'Iron', 4, 'main_hand'),
(78, 'weapon', 1, 'Rapier', 'Held in the main hand.', 133, 133, 1.8, 211, 105, 5, 15, 0, NULL, 'Steel', 5, 'main_hand'),
(79, 'weapon', 1, 'Dagger', 'Held in either hand. Fits the main hand or the off hand.', 67, 67, 0.9, 24, 12, 2, 3, 0, NULL, 'Bronze', 2, 'either_hand'),
(80, 'weapon', 1, 'Knife', 'Held in either hand. Fits the main hand or the off hand.', 72, 72, 0.6, 34, 17, 1, 3, 0, NULL, 'Iron', 1, 'either_hand'),
(81, 'weapon', 1, 'Stiletto', 'Held in either hand. Fits the main hand or the off hand.', 112, 112, 0.4, 106, 53, 3, 7, 0, NULL, 'Steel', 3, 'either_hand'),
(82, 'armor', 1, 'Buckler', 'Held in the off hand. Blocks blows.', 40, 40, 0.9, 11, 5, 2, 0, 3, NULL, 'Wood', 2, 'off_hand'),
(83, 'armor', 1, 'Kite Shield', 'Held in the off hand. Blocks blows.', 98, 98, 4.8, 71, 35, 4, 0, 9, NULL, 'Iron', 4, 'off_hand'),
(84, 'armor', 1, 'Tower Shield', 'Held in the off hand. Blocks blows.', 144, 144, 9.9, 222, 111, 6, 0, 19, NULL, 'Steel', 6, 'off_hand'),
(85, 'weapon', 1, 'Greatsword', 'Needs both hands. The off hand must be empty.', 133, 133, 6.6, 330, 165, 5, 24, 0, NULL, 'Steel', 5, 'two_hand'),
(86, 'weapon', 1, 'Warhammer', 'Needs both hands. The off hand must be empty.', 98, 98, 7.2, 134, 67, 4, 15, 0, NULL, 'Iron', 4, 'two_hand'),
(87, 'weapon', 1, 'Battleaxe', 'Needs both hands. The off hand must be empty.', 82, 82, 5.5, 78, 39, 4, 12, 0, NULL, 'Bronze', 4, 'two_hand');
