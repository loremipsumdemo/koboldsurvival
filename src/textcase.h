#pragma once
#include <string>

// Upper case for UTF-8 text, for the title screen's and menus' capital-letter labels. It covers the letters of the
// game's languages that have case: Latin (with Latin-1, Latin Extended-A, Romanian and Vietnamese letters), Greek and
// Cyrillic. Everything else (CJK, Thai, Arabic, digits, punctuation) is left as it is, and so is a byte sequence that is
// not valid UTF-8.
inline std::string uiUpper(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        const unsigned char c0 = (unsigned char)in[i];
        unsigned cp = 0;
        size_t len = 1;
        if (c0 < 0x80) { cp = c0; }
        else if ((c0 & 0xE0) == 0xC0 && i + 1 < in.size()) { cp = ((c0 & 0x1Fu) << 6) | ((unsigned char)in[i + 1] & 0x3Fu); len = 2; }
        else if ((c0 & 0xF0) == 0xE0 && i + 2 < in.size()) {
            cp = ((c0 & 0x0Fu) << 12) | (((unsigned char)in[i + 1] & 0x3Fu) << 6) | ((unsigned char)in[i + 2] & 0x3Fu);
            len = 3;
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < in.size()) {
            out.append(in, i, 4);
            i += 4;
            continue;
        } else {
            out += (char)c0;
            ++i;
            continue;
        }
        unsigned up = cp;
        if (cp >= 'a' && cp <= 'z') up = cp - 32;
        else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) up = cp - 0x20;                        // Latin-1
        else if (cp == 0xFF) up = 0x178;
        else if (cp >= 0x100 && cp <= 0x137 && (cp & 1)) up = cp - 1;                           // Latin Extended-A
        else if (cp >= 0x139 && cp <= 0x148 && !(cp & 1)) up = cp - 1;
        else if (cp >= 0x14A && cp <= 0x177 && (cp & 1)) up = cp - 1;
        else if ((cp == 0x17A || cp == 0x17C || cp == 0x17E)) up = cp - 1;
        else if (cp == 0x131) up = 'I';
        else if (cp == 0x1A1 || cp == 0x1B0) up = cp - 1;                                        // Vietnamese ơ ư
        else if (cp >= 0x218 && cp <= 0x21B && (cp & 1)) up = cp - 1;                           // Romanian ș ț
        else if (cp >= 0x3B1 && cp <= 0x3C9 && cp != 0x3C2) up = cp - 0x20;                    // Greek
        else if (cp == 0x3C2) up = 0x3A3;
        else if (cp == 0x3AC) up = 0x386;
        else if (cp >= 0x3AD && cp <= 0x3AF) up = cp - 0x25;
        else if (cp == 0x3CC) up = 0x38C;
        else if (cp == 0x3CD || cp == 0x3CE) up = cp - 0x3F;
        else if (cp >= 0x430 && cp <= 0x44F) up = cp - 0x20;                                    // Cyrillic
        else if (cp >= 0x450 && cp <= 0x45F) up = cp - 0x50;
        else if (cp == 0x491) up = 0x490;
        else if (cp >= 0x1EA0 && cp <= 0x1EFF && (cp & 1)) up = cp - 1;                         // Vietnamese
        if (up == cp) {
            out.append(in, i, len);
        } else if (up < 0x80) {
            out += (char)up;
        } else if (up < 0x800) {
            out += (char)(0xC0 | (up >> 6));
            out += (char)(0x80 | (up & 0x3F));
        } else {
            out += (char)(0xE0 | (up >> 12));
            out += (char)(0x80 | ((up >> 6) & 0x3F));
            out += (char)(0x80 | (up & 0x3F));
        }
        i += len;
    }
    return out;
}
