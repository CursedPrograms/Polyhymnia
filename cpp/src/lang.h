// Polyhymnia - language packs (see lang/): every UI string goes through T("English").
#pragma once
#include <string>
#include <vector>

namespace Lang {
struct Pack { std::string code, name; };
const std::vector<Pack>& packs();
bool load(const std::string& dir, const std::string& code);
const std::string& current();
bool needsCJK();
}
const char* T(const char* english);