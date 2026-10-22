#include "items.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include "pak.h"

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Removes -- comments. Text inside quotes is left alone, so a '--' in a description survives.
// A comment runs to the end of its line, and the line break is kept.
std::string stripComments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool inQuote = false;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (inQuote) {
            out += c;
            if (c == '\'') {
                if (i + 1 < in.size() && in[i + 1] == '\'') {   // '' is an escaped quote
                    out += in[++i];
                } else {
                    inQuote = false;
                }
            }
            continue;
        }
        if (c == '\'') {
            inQuote = true;
            out += c;
            continue;
        }
        if (c == '-' && i + 1 < in.size() && in[i + 1] == '-') {
            while (i < in.size() && in[i] != '\n') ++i;
            if (i < in.size()) out += '\n';
            continue;
        }
        out += c;
    }
    return out;
}

// Splits the text into statements at each ';' that isn't inside quotes
std::vector<std::string> splitStatements(const std::string& sql) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    for (char c : sql) {
        // An escaped '' toggles the flag twice, so it stays inside the string
        if (c == '\'') inQuote = !inQuote;
        if (c == ';' && !inQuote) {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    out.push_back(cur);
    return out;
}

// One value in an INSERT row. Quoted values keep their text; unquoted ones are numbers or NULL.
struct Token {
    std::string text;
    bool quoted = false;
};

// Reads one "( ... )" row starting at pos, which must point at '('. Leaves pos after the ')'.
bool readRow(const std::string& s, size_t& pos, std::vector<Token>& row) {
    if (pos >= s.size() || s[pos] != '(') return false;
    ++pos;
    Token cur;
    bool inQuote = false;
    while (pos < s.size()) {
        const char c = s[pos];
        if (inQuote) {
            if (c == '\'') {
                if (pos + 1 < s.size() && s[pos + 1] == '\'') {
                    cur.text += '\'';
                    pos += 2;
                    continue;
                }
                inQuote = false;
                ++pos;
                continue;
            }
            cur.text += c;
            ++pos;
            continue;
        }
        if (c == '\'') {
            inQuote = true;
            cur.quoted = true;
            ++pos;
            continue;
        }
        if (c == ',') {
            row.push_back(cur);
            cur = Token{};
            ++pos;
            continue;
        }
        if (c == ')') {
            row.push_back(cur);
            ++pos;
            return true;
        }
        if (!std::isspace((unsigned char)c)) cur.text += c;   // spaces outside quotes are not part of a value
        ++pos;
    }
    return false;   // the text ended before the row closed
}

bool toInt(const Token& t, int& v) {
    if (t.quoted || t.text.empty()) return false;
    char* end = nullptr;
    const long long n = std::strtoll(t.text.c_str(), &end, 10);
    if (*end != '\0') return false;
    v = (int)n;
    return true;
}

bool toFloat(const Token& t, float& v) {
    if (t.quoted || t.text.empty()) return false;
    char* end = nullptr;
    const double d = std::strtod(t.text.c_str(), &end);
    if (*end != '\0') return false;
    v = (float)d;
    return true;
}

bool isNull(const Token& t) {
    return !t.quoted && upper(t.text) == "NULL";
}

// Builds one item from a row, matching each value to its column name
bool toItem(const std::vector<std::string>& cols, const std::vector<Token>& row, ItemDef& item, std::string& err) {
    if (cols.size() != row.size()) {
        err = "a row has " + std::to_string(row.size()) + " values but the column list has " + std::to_string(cols.size());
        return false;
    }

    std::unordered_map<std::string, size_t> at;
    for (size_t i = 0; i < cols.size(); ++i) at[cols[i]] = i;

    auto get = [&](const char* name) -> const Token* {
        auto it = at.find(name);
        if (it == at.end()) return nullptr;
        const Token& t = row[it->second];
        return isNull(t) ? nullptr : &t;
    };
    auto needInt = [&](const char* name, int& v) {
        const Token* t = get(name);
        if (!t) return true;   // absent or NULL: the default stays
        if (!toInt(*t, v)) { err = std::string("bad number in column ") + name + ": " + t->text; return false; }
        return true;
    };
    auto needFloat = [&](const char* name, float& v) {
        const Token* t = get(name);
        if (!t) return true;
        if (!toFloat(*t, v)) { err = std::string("bad number in column ") + name + ": " + t->text; return false; }
        return true;
    };
    auto needText = [&](const char* name, std::string& v) {
        const Token* t = get(name);
        if (t) v = t->text;
        return true;
    };

    if (!needInt("id", item.id)) return false;
    if (item.id <= 0) { err = "item id must be a positive number"; return false; }
    if (!needText("type_id", item.typeId)) return false;
    if (item.typeId != "item" && item.typeId != "consumable" && item.typeId != "weapon" &&
        item.typeId != "armor" && item.typeId != "accessory") {
        err = "unknown type_id '" + item.typeId + "' on item " + std::to_string(item.id);
        return false;
    }
    if (!needText("name", item.name)) return false;
    if (item.name.empty()) { err = "item " + std::to_string(item.id) + " has no name"; return false; }
    needText("description", item.description);

    if (!needInt("max_stack_size", item.maxStackSize)) return false;
    if (!needInt("max_durability", item.maxDurability)) return false;
    if (!needInt("current_durability", item.currentDurability)) return false;
    if (!needFloat("weight", item.weight)) return false;
    if (!needInt("buy_value", item.buyValue)) return false;
    if (!needInt("sell_value", item.sellValue)) return false;
    if (!needInt("required_level", item.requiredLevel)) return false;
    if (!needInt("attack_value", item.attackValue)) return false;
    if (!needInt("defense_value", item.defenseValue)) return false;

    if (item.maxStackSize < 1) { err = "item " + std::to_string(item.id) + " needs a max_stack_size of at least 1"; return false; }
    return true;
}

} // namespace

bool parseItemSql(const std::string& sql, std::vector<ItemDef>& out, std::string& err) {
    out.clear();
    err.clear();
    std::unordered_set<int> ids;

    for (const std::string& raw : splitStatements(stripComments(sql))) {
        const std::string stmt = trim(raw);
        if (stmt.empty()) continue;

        // Only INSERT statements hold rows; CREATE TABLE and anything else is skipped
        const std::string up = upper(stmt);
        if (up.compare(0, 6, "INSERT") != 0) continue;

        // Column list: the first parenthesised group, which has no nested brackets
        const size_t colOpen = stmt.find('(');
        const size_t colClose = colOpen == std::string::npos ? std::string::npos : stmt.find(')', colOpen);
        if (colClose == std::string::npos) { err = "INSERT has no column list"; return false; }
        std::vector<std::string> cols;
        {
            std::string name;
            for (size_t i = colOpen + 1; i <= colClose; ++i) {
                const char c = stmt[i];
                if (c == ',' || i == colClose) {
                    cols.push_back(trim(name));
                    name.clear();
                } else {
                    name += c;
                }
            }
        }

        const size_t valuesAt = up.find("VALUES");
        if (valuesAt == std::string::npos) { err = "INSERT has no VALUES"; return false; }

        // Rows: "(...)" groups separated by commas
        size_t pos = valuesAt + 6;
        while (true) {
            while (pos < stmt.size() && (std::isspace((unsigned char)stmt[pos]) || stmt[pos] == ',')) ++pos;
            if (pos >= stmt.size()) break;

            std::vector<Token> row;
            if (!readRow(stmt, pos, row)) { err = "malformed row near: " + stmt.substr(pos, 40); return false; }

            ItemDef item;
            if (!toItem(cols, row, item, err)) return false;
            if (!ids.insert(item.id).second) { err = "item id " + std::to_string(item.id) + " appears twice"; return false; }
            out.push_back(item);
        }
    }
    return true;
}

const std::vector<ItemDef>& itemDatabase() {
    static const std::vector<ItemDef> db = [] {
        std::vector<ItemDef> items;
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        if (!pakFind("data/items.sql", bytes, size)) {
            std::fprintf(stderr, "items: data/items.sql is not in the pak\n");
            return items;
        }
        std::string err;
        const std::string sql((const char*)bytes, size);
        if (!parseItemSql(sql, items, err)) {
            std::fprintf(stderr, "items: %s\n", err.c_str());
            items.clear();
        }
        return items;
    }();
    return db;
}

const ItemDef* findItem(int id) {
    if (id <= 0) return nullptr;
    for (const ItemDef& d : itemDatabase()) {
        if (d.id == id) return &d;
    }
    return nullptr;
}
