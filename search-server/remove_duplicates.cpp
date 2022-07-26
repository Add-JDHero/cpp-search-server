#include "remove_duplicates.h"

using namespace std;

void RemoveDuplicates(SearchServer& search_server) {
    set<int> duplicates;
    set<set<string>> documents;
    for (const int document_id: search_server) {
        const auto document = search_server.GetWordFrequencies(document_id);
        set<string> document_words;

        for (auto [word, freq]: document) {
            document_words.insert(word);
        }

        bool is_duplicate = false;
        for (auto i = documents.begin(); i != documents.end(); i++) {
            if (*i == document_words) {
                is_duplicate = true;
            }
        }

        if (is_duplicate) {
            duplicates.insert(document_id);
        } else {
            documents.insert(document_words);
        }
    }

    for (auto id : duplicates) {
        cout << "Found duplicate document id "s << id << endl;
        search_server.RemoveDocument(id);
    }
}