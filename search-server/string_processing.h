#pragma once
#include <set>
#include <string>
#include <vector>

template <typename StringContainer>
std::set<std::string, std::less<>> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
    std::set<std::string, std::less<>> non_empty_strings{};
    for (const auto& sv : strings) { 
        if (sv != "") { 
            non_empty_strings.insert(std::string{ sv });
        }
    }
    return non_empty_strings;
}
std::vector<std::string_view> SplitIntoWords(std::string_view str);