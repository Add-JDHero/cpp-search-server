#include"string_processing.h"

std::vector<std::string_view> SplitIntoWords(const std::string_view str) {
    std::vector<std::string_view> result;
    int64_t pos = 0;
    const int64_t pos_end = str.npos;
    while (true) {
        int64_t space = str.find(' ', pos);
        result.push_back(space == pos_end ? str.substr(pos) : str.substr(pos, space - pos));
        if (space == pos_end) {
            break;
        }
        else {
            pos = space + 1;
        }
    }

    return result;
}