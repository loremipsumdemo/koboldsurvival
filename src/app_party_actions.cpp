#include "app.h"

using namespace play;

void App::partyRandomUnit() {
    const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
    if (party[picked].mob != MobKind::None) {
        if (partyUi.editable) party[picked] = mobMember(party[picked].mob, partyRng);
        stopNameEdit();
        closeDropdown();
        return;
    }
    std::vector<std::string> others;
    for (int i = 0; i < (int)party.size(); ++i) {
        if (i != picked) others.push_back(party[i].name);
    }
    PartyMember fresh = randomMember(partyRng, others);
    if (!partyUi.editable) {
        const PartyMember& old = party[picked];
        fresh.stats = old.stats;
        fresh.gear = old.gear;
        fresh.items = old.items;
        fresh.hunger = old.hunger;
    }
    party[picked] = fresh;
    stopNameEdit();
    closeDropdown();
    return;
}

void App::partyDropPick(int param) {
    const std::vector<DropEntry> entries = dropdownEntries(party, partyUi);
    if (param < 0 || param >= (int)entries.size()) return;
    PartyMember& m = pickedMember(party, partyUi);
    const int value = entries[param].value;
    switch (partyUi.dropKind) {
    case DropKind::Sprite:
        m.sprite = value;
        break;
    case DropKind::Body:
        m.body = (BodyType)value;
        m.sprite = modelForBody(m.body);
        break;
    case DropKind::Gear:
        if (partyUi.editable) m.gear[partyUi.dropSlot] = value;
        break;
    case DropKind::Item: {
        if (!partyUi.editable) break;
        ItemStack& st = m.items[partyUi.dropSlot];
        if (value == 0) {
            st = ItemStack{};
        } else {
            const int count = st.item == value ? st.count : 1;
            st = ItemStack{ value, std::clamp(count, 1, stackLimit(value)) };
        }
        break;
    }
    case DropKind::MobKind:
        if (partyUi.dropSlot < 0) {
            if (partyUi.resizable && (int)party.size() < PARTY_MAX_UNITS) {
                party.push_back(mobMember((MobKind)value, partyRng));
                partyUi.selected = (int)party.size() - 1;
                partyUi.listScroll = (int)party.size();
            }
        } else if (partyUi.mobKindEditable && m.mob != MobKind::None && m.mob != (MobKind)value) {
            const bool renamed = cleanName(m.name) != lang(mobInfo(m.mob).nameKey);
            const std::string name = m.name;
            m = mobMember((MobKind)value, partyRng);
            if (renamed) m.name = name;
        }
        break;
    case DropKind::None:
        break;
    }
    closeDropdown();
    return;
}

bool App::partyAction(Action action, int param) {
    switch (action) {
    case Action::PartyPick:
        partyUi.selected = std::clamp(param, 0, (int)party.size() - 1);
        stopNameEdit();
        closeDropdown();
        return true;
    case Action::PartyTabPick:
        partyUi.tab = (PartyTab)std::clamp(param, 0, PARTY_TAB_COUNT - 1);
        stopNameEdit();
        closeDropdown();
        return true;
    case Action::PartyNameField:
        closeDropdown();
        if (!partyUi.editingName) SDL_StartTextInput(window);
        partyUi.editingName = true;
        return true;
    case Action::PartyBodyBox:
        stopNameEdit();
        partyUi.dropKind = DropKind::Body;
        partyUi.dropScroll = 0;
        return true;
    case Action::PartySpriteBox:
        stopNameEdit();
        partyUi.dropKind = DropKind::Sprite;
        partyUi.dropScroll = clampDropScroll(pickedMember(party, partyUi).sprite - 1 - DROP_VISIBLE_ROWS / 2,
                                             UNIT_SPRITE_COUNT);
        return true;
    case Action::PartyMobBox:
        if (!partyUi.mobKindEditable) return true;
        stopNameEdit();
        partyUi.dropKind = DropKind::MobKind;
        partyUi.dropSlot = 0;
        partyUi.dropScroll = 0;
        return true;
    case Action::PartyAddMob:
        if (!partyUi.resizable || (int)party.size() >= PARTY_MAX_UNITS) return true;
        stopNameEdit();
        partyUi.dropKind = DropKind::MobKind;
        partyUi.dropSlot = -1;
        partyUi.dropScroll = 0;
        return true;
    case Action::PartyRandomUnit:
        partyRandomUnit();
        return true;
    case Action::PartyGearSlot:
        if (!partyUi.editable) return true;
        stopNameEdit();
        partyUi.dropKind = DropKind::Gear;
        partyUi.dropSlot = std::clamp(param, 0, GEAR_SLOTS - 1);
        partyUi.dropScroll = 0;
        return true;
    case Action::PartyStep: {
        if (!partyUi.editable) return true;
        const StatId stat = (StatId)(param / 2);
        const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 10 : 1;
        Stats& st = pickedMember(party, partyUi).stats;
        setStatLevel(st, stat, levelOf(st, stat) + ((param % 2) != 0 ? step : -step));
        return true;
    }
    case Action::PartyInvSlot:
        stopNameEdit();
        partyUi.stackSlot = std::clamp(param, 0, INVENTORY_SLOTS - 1);
        if (!partyUi.editable) return true;
        partyUi.dropKind = DropKind::Item;
        partyUi.dropSlot = partyUi.stackSlot;
        partyUi.dropScroll = 0;
        return true;
    case Action::PartyCount: {
        if (!partyUi.editable) return true;
        ItemStack& st = pickedMember(party, partyUi).items[partyUi.stackSlot];
        if (st.item > 0) st.count = std::clamp(st.count + param, 1, stackLimit(st.item));
        return true;
    }
    case Action::PartyDropPick:
        partyDropPick(param);
        return true;
    case Action::PartyDropScroll: {
        const int count = (int)dropdownEntries(party, partyUi).size();
        partyUi.dropScroll = clampDropScroll(partyUi.dropScroll + param, count);
        return true;
    }
    case Action::PartyDropBack:
    case Action::PartyHeading:
    case Action::PartyNote:
    case Action::PartyStatLabel:
    case Action::PartyMessage:
        return true;
    case Action::PartyAddUnit:
        if (partyUi.resizable && (int)party.size() < PARTY_MAX_UNITS) {
            addRandomMember(party, partyRng);
            partyUi.selected = (int)party.size() - 1;
            partyUi.listScroll = (int)party.size();
            stopNameEdit();
            closeDropdown();
        }
        return true;
    case Action::PartyRemoveUnit:
        if (partyUi.resizable && !party.empty()) {
            const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
            party.erase(party.begin() + picked);
            partyUi.selected = std::clamp(picked, 0, (int)party.size() - 1);
            stopNameEdit();
            closeDropdown();
        }
        return true;
    case Action::PartyRandomAll:
        party = scenarioParty(scenarioPick, partyRng, partyPeople(party));
        stopNameEdit();
        closeDropdown();
        return true;
    case Action::PartyConfirm:
        if (party.empty() || !partyNamesValid(party)) return true;
        stopNameEdit();
        closeDropdown();
        startMapLevel(pendingMap);
        return true;
    default:
        return false;
    }
}
