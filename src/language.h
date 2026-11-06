#pragma once
#include <initializer_list>
#include <string>
#include <vector>

inline constexpr const char* LANGUAGE_DEFAULT_ID = "language_en";
inline constexpr const char* LANGUAGE_FOLDER = "LANGUAGE";

bool validLanguageId(const std::string& id);

struct LanguageChoice {
    std::string id;
    std::string name;
    std::string label;
};

const std::vector<LanguageChoice>& languageChoices();
void rescanLanguages();

std::string languageDisplayName();

bool loadLanguage(const std::string& id, std::string& err);

enum class NameSet { Masculine, Feminine, Androgynous };

std::vector<std::string> languageNames(NameSet set);

std::string lang(const std::string& key);

std::string langf(const std::string& key, std::initializer_list<std::string> args);
