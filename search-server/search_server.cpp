#include "search_server.h"

using namespace std;

SearchServer::SearchServer(const string stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))  // Invoke delegating constructor
        // from string container
{
}

void SearchServer::AddDocument(int document_id, const string_view document, DocumentStatus status, const vector<int>& ratings) {
    if (!IsValidWord(document)) {
        throw invalid_argument("Document contains special symbols"s);
    } 
    else if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }
    const auto words = SplitIntoWordsNoStop(document);

    const double inv_word_count = 1.0 / static_cast<int>(words.size());
    for (const string_view word : words) {
        word_to_document_freqs_[string(word)][document_id] += inv_word_count;
        const auto it = word_to_document_freqs_.find(string(word));
        document_to_word_freqs_[document_id][it->first] += inv_word_count;
    }
    documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
    document_ids_.emplace(document_id);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

const map<string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static const map<string_view, double> empty_words = {};

    if (document_to_word_freqs_.count(document_id)) {
        return document_to_word_freqs_.at(document_id);
    }
    return empty_words;
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query) const {
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}


SearchServer::matched_tuple SearchServer::MatchDocument(string_view raw_query, int document_id) const {
    return MatchDocument(execution::seq, raw_query, document_id);
}

SearchServer::matched_tuple SearchServer::MatchDocument(const execution::sequenced_policy& policy, 
                                                                 string_view raw_query, int document_id) const {

    if (document_ids_.count(document_id) == 0)
    {
        using namespace std::string_literals;
        throw std::out_of_range("Sqe out of range"s);
    }

    const auto query = ParseQuery(raw_query, true);
    auto status = documents_.at(document_id).status;
    std::vector<std::string_view> matched_words;
    for (const std::string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(string(word)) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(string(word)).count(document_id)) {
            return {matched_words, status};
        }
    }

    for (const std::string_view word : query.plus_words) {
        if (word_to_document_freqs_.count(string(word)) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(string(word)).count(document_id)) {
            matched_words.push_back(word);
        }
    }

    return {matched_words, documents_.at(document_id).status};
}

SearchServer::matched_tuple SearchServer::MatchDocument(const std::execution::parallel_policy& policy, 
                                                                std::string_view raw_query, int document_id) const {
    
    if (!document_ids_.count(document_id))
    {
        using namespace std::string_literals;
        throw std::out_of_range("Par out of range"s);
    }

    auto query = ParseQuery(raw_query, false);
    const auto status = documents_.at(document_id).status;
    const auto word_checker = [this, document_id](const std::string_view word) {
        const auto it = word_to_document_freqs_.find(string(word));
        return it != word_to_document_freqs_.end() && it->second.count(document_id);
    };

    if (std::any_of(std::execution::par, query.minus_words.begin(), query.minus_words.end(), word_checker)) {
        std::vector<std::string_view> m;
        return {m, status};
    }

    std::vector<std::string_view> matched_words(query.plus_words.size());
    auto words_end = std::copy_if(std::execution::par,
        query.plus_words.begin(), query.plus_words.end(),
        matched_words.begin(),
        word_checker);
    std::sort(std::execution::par, matched_words.begin(), words_end);
    words_end = std::unique(std::execution::par, matched_words.begin(), words_end);
    matched_words.erase(words_end, matched_words.end());

    return {matched_words, status};
}

void SearchServer::RemoveDocument(int document_id) {
    for (auto& [word, freq]: document_to_word_freqs_.at(document_id)) {
        word_to_document_freqs_.at(string(word)).erase(document_id);        
    }
    document_to_word_freqs_.erase(document_id);  
    document_ids_.erase(document_id);
    documents_.erase(document_id);
}

void SearchServer::RemoveDocument(const execution::sequenced_policy& policy, int document_id) {
    SearchServer::RemoveDocument(document_id);
}

void SearchServer::RemoveDocument(const execution::parallel_policy& policy, int document_id) {

    if (!document_to_word_freqs_.count(document_id)) {
        return;
    }

    std::vector<std::string_view> words(document_to_word_freqs_.at(document_id).size());
    std::transform(policy,
        document_to_word_freqs_.at(document_id).begin(), document_to_word_freqs_.at(document_id).end(),
        words.begin(),
        [](const auto& p) {
            return string_view(p.first);
        }
    );
    std::for_each(policy, 
        words.begin(), 
        words.end(),
        [&](const string_view word) {
            word_to_document_freqs_.at(std::string(word)).erase(document_id);
        }
    );
    
    document_ids_.erase(document_id);
    documents_.erase(document_id);
}

bool SearchServer::IsStopWord(const string_view word) const {
    return stop_words_.count(string(word)) > 0;
}

bool SearchServer::IsValidWord(const string_view word) {
    // A valid word must not contain special characters
    return none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
    });
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(const string_view text) const {
    vector<string_view> words;
    for (const string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("Word "s + string(word) + " is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

void AddDocument(SearchServer& search_server, int document_id, const string& document, DocumentStatus status,
    const vector<int>& ratings) {
    search_server.AddDocument(document_id, document, status, ratings);
}

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = 0;
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(string_view text) const {
    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    bool is_minus = false;
    string_view word = text;
    if (word[0] == '-') {
        is_minus = true;
        word = word.substr(1);
    }
    if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
        throw invalid_argument("Query word "s + word.data() + " is invalid");
    }

    return {word, is_minus, IsStopWord(word)};
}

SearchServer::Query SearchServer::ParseQuery(string_view text, bool sort_flag) const {
    Query result;
    
    for (const std::string_view word : SplitIntoWords(text)) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            } else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }

    if (sort_flag) {
        sort(execution::seq, result.minus_words.begin(), result.minus_words.end());
        sort(execution::seq, result.plus_words.begin(), result.plus_words.end());

        auto it1 = unique(execution::seq, result.minus_words.begin(), result.minus_words.end());
        auto it2 = unique(execution::seq, result.plus_words.begin(), result.plus_words.end());

        result.minus_words.erase(it1, result.minus_words.end());
        result.plus_words.erase(it2, result.plus_words.end());
    }
   
    return result;
}

// Existence required
double SearchServer::ComputeWordInverseDocumentFreq(const string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(string(word)).size());
}