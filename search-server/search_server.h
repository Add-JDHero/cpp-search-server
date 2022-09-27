#pragma once

#include <algorithm>
#include <execution>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <stack> 
#include <string_view>

#include "document.h"
#include "string_processing.h"
#include "concurrent_map.h"

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double EPSILON = 1e-6;

class SearchServer {
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);
    explicit SearchServer(const std::string stop_words_text);

    void AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    template <typename ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query, 
                                                    DocumentPredicate document_predicate) const;

    template<typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query, DocumentStatus status) const;

    template <typename ExecutionPolicy> 
    std::vector<Document> FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query) const;

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const;
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentStatus status) const;
    std::vector<Document> FindTopDocuments(std::string_view raw_query) const;

    int GetDocumentCount() const;
    
    std::set<int>::const_iterator begin() {
        return document_ids_.begin();
    }

    std::set<int>::const_iterator end() {
        return document_ids_.end();
    }

    using matched_tuple = std::tuple<std::vector<std::string_view>, DocumentStatus>;
    matched_tuple MatchDocument( std::string_view raw_query, int document_id) const;
    matched_tuple MatchDocument(const std::execution::sequenced_policy& policy, 
                                                                 std::string_view raw_query, int document_id) const;
    matched_tuple MatchDocument(const std::execution::parallel_policy& policy, 
                                                                 std::string_view raw_query, int document_id) const;

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;

    void RemoveDocument(int document_id);
    void RemoveDocument(const std::execution::sequenced_policy& policy, int document_id);
	void RemoveDocument(const std::execution::parallel_policy& policy, int document_id);

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };
    const std::set<std::string, std::less<>> stop_words_;
    std::map<std::string, std::map<int, double>> word_to_document_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;

    bool IsStopWord(const std::string_view word) const;
    static bool IsValidWord(const std::string_view word);

    std::vector<std::string_view> SplitIntoWordsNoStop(const std::string_view text) const;
    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(const std::string_view text) const;

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    Query ParseQuery(const std::string_view text, bool sort_flag) const;
    Query ParseQuery(const std::execution::parallel_policy& policy,
                                            const std::string_view text) const;

    double ComputeWordInverseDocumentFreq(const std::string_view word) const;

    template<typename ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(ExecutionPolicy& policy, const SearchServer::Query& query,
                                            DocumentPredicate document_predicate) const;
};

//          TEMPLATE FUNCTIONS REALIZATION

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words) 
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {
    if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
            throw std::invalid_argument("control character in stop-words");
    }
}

template <typename ExecutionPolicy> 
std::vector<Document> SearchServer::FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(
        policy,
        raw_query,
        [status](int document_id, DocumentStatus document_status, int rating) {
            return document_status == status;
        }
    );
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const {
    return FindTopDocuments(std::execution::seq, raw_query, document_predicate);
}

template <typename ExecutionPolicy, typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query, 
                                                    DocumentPredicate document_predicate) const {
    const auto query = ParseQuery(raw_query, true);

    auto matched_documents = FindAllDocuments(policy, query, document_predicate);

    std::sort(policy, matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
        if (std::abs(lhs.relevance - rhs.relevance) < EPSILON) {
            return lhs.rating > rhs.rating;
        } else {
            return lhs.relevance > rhs.relevance;
        }
    });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }

    return matched_documents;
}

template<typename ExecutionPolicy, typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(ExecutionPolicy& policy, const SearchServer::Query& query,
    DocumentPredicate document_predicate) const {
    using namespace std;
    
    ConcurrentMap<int, double> document_to_relevance_par(50);
    const double documents_count = GetDocumentCount();


    for_each(policy, query.plus_words.begin(), query.plus_words.end(),
             [this, &document_to_relevance_par, documents_count, document_predicate](string_view word){
                if (word_to_document_freqs_.count(std::string(word))) {
                    const double inverse_document_freq = ComputeWordInverseDocumentFreq(std::string(word));
                    for (const auto& [document_id, term_freq] : word_to_document_freqs_.at(std::string(word))) { 
                        const auto &document = documents_.at(document_id);
                        if (document_predicate(document_id, document.status, document.rating)) {
                            document_to_relevance_par[document_id].ref_to_value += term_freq * inverse_document_freq;
                        }
                    }
                }
    });

    map<int, double> document_to_relevance = document_to_relevance_par.BuildOrdinaryMap();

    for_each(query.minus_words.begin(), query.minus_words.end(),
             [this, &document_to_relevance](string_view word){
                if (word_to_document_freqs_.count(std::string(word)) != 0) {
                    for (const auto& [document_id, term_freq] : word_to_document_freqs_.at(std::string(word))) {
                        if (document_to_relevance.count(document_id)) {
                            document_to_relevance.erase(document_id);
                        }
                    }
                }
    });

    vector<Document> matched_documents;
    matched_documents.reserve(document_to_relevance.size());
    for (const auto& [document_id, relevance] : document_to_relevance) {
        matched_documents.push_back(Document{document_id, relevance, documents_.at(document_id).rating});
    }
    return matched_documents;
}

void AddDocument(SearchServer& search_server, int document_id, const std::string_view document, DocumentStatus status,
    const std::vector<int>& ratings);

