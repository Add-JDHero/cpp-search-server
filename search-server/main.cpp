#include <iostream>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <string>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double EPSILON = 1e-6;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}
    
struct Document {
    int id;
    double relevance;
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

//  -------- main object -------- 

class SearchServer {
public:
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }    
    
    void AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id, 
            DocumentData{
                ComputeAverageRating(ratings), 
                status
            });
    }

    template <typename Predicate>
    vector<Document> FindTopDocuments(const string& raw_query, Predicate predicate) const {            
        const auto query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, predicate);
        
        sort(matched_documents.begin(), matched_documents.end(),
             [](const Document& lhs, const Document& rhs) {
                if (abs(lhs.relevance - rhs.relevance) < EPSILON) {
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
    
    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(
            raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
                return document_status == status;
            });
    }
 
    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const {
        return documents_.size();
    }
    
    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query, int document_id) const {
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return {matched_words, documents_.at(document_id).status};
    }
    
private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;
    
    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }
    
    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }
    
    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }
    
    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };
    
    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        // Word shouldn't be empty
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        return {
            text,
            is_minus,
            IsStopWord(text)
        };
    }
    
    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };
    
    Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                } else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }
    
    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }
    
    template <typename Predicate>
    vector<Document> FindAllDocuments(const Query& query, Predicate predicate) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (predicate(document_id, document_data.status, document_data.rating)) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }
        
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back({
                document_id,
                relevance,
                documents_.at(document_id).rating
            });
        }
        return matched_documents;
    }
}; //   --------  main object-------- 


template <typename Key, typename Value>
ostream& Print(ostream& out, map<Key, Value> dictionary) {
    bool first_word = true;
    for (auto& [key, value]: dictionary) {
        if (!first_word) {
           out << ", ";
        }
        out << "" << key << ": " << value;
        first_word = false;
    }
    return out;
}

template <typename Collection>
ostream& Print(ostream& out, const Collection container) {
    bool first_word = true;
    for (auto& word: container) {
        if (!first_word) {
            out << ", ";
        }
        out << word;
        first_word = false;
    }
    
    return out;
}

template <typename Term>
ostream& operator<<(ostream& out, const vector<Term> container) {
    out << "[";
    Print(out, container);
    out << "]";
    return out;
}

template <typename Term1>
ostream& operator<<(ostream& out, const set<Term1>& container) {
    out << "{";
    Print(out, container);
    out << "}";    
    return out;
}

template <typename Key, typename Value>
ostream& operator<<(ostream& out, const map<Key, Value> dictionary){
    out << "{";
    Print(out, dictionary);
    out << "}";
    return out;
}
template <typename T, typename U>
void AssertEqualImpl(const T& first_operand, const U& second_operand, const string& first_operand_str, const string& second_operand_str, const string& file,
                     const string& func, unsigned line, const string& hint) {
    if (first_operand != second_operand) {
        cout << boolalpha;
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT_EQUAL("s << first_operand_str << ", "s << second_operand_str << ") failed: "s;
        cout << first_operand << " != "s << second_operand << "."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))

void AssertImpl(bool value, const string& expr_str, const string& file, const string& func, unsigned line,
                const string& hint) {
    if (!value) {
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

#define ASSERT(expr) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))

vector<int> TakeEvens(const vector<int>& numbers) {
    vector<int> evens;
    for (int x : numbers) {
        if (x % 2 == 0) {
            evens.push_back(x);
        }
    }
    return evens;
}

map<string, int> TakeAdults(const map<string, int>& people) {
    map<string, int> adults;
    for (const auto& [name, age] : people) {
        if (age >= 18) {
            adults[name] = age;
        }
    }
    return adults;
}

bool IsPrime(int n) {
    if (n < 2) {
        return false;
    }
    int i = 2;
    while (i * i <= n) {
        if (n % i == 0) {
            return false;
        }
        ++i;
    }
    return true;
}

set<int> TakePrimes(const set<int>& numbers) {
    set<int> primes;
    for (int number : numbers) {
        if (IsPrime(number)) {
            primes.insert(number);
        }
    }
    return primes;
}

// -------- Start of search engine unit tests ----------

// The test verifies that the search engine excludes stop words when adding documents
void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    {
        SearchServer server;
        server.SetStopWords("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_HINT(server.FindTopDocuments("in"s).empty(), "Stop words must be excluded from documents"s);
    }
}

void TestAddingDocuments() {
    SearchServer server;
    
    server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});
    server.AddDocument(1, "пушистый кот пушистый хвост"s,       DocumentStatus::ACTUAL, {7, 2, 7});
    server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
    server.AddDocument(3, "ухоженный скворец евгений"s,         DocumentStatus::BANNED, {9});
    
    ASSERT(static_cast<int>(server.GetDocumentCount()) == 4);
}

void TestMatching() {
    {
        SearchServer server;
        server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});
        
        //  Checking the return of query words without negative words
        const auto info = get<0>(server.MatchDocument("белый кот"s, 0));
        ASSERT(info.size() == 2);
    }
    
    {
        SearchServer server;
        server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});

        //  Checking the return of the c query words taking into account the negative words
        const auto info = get<0>(server.MatchDocument("белый -кот"s, 0));
        ASSERT(info.empty());
    }
}

void TestRelevanceSort() {
    {
        SearchServer server;
        server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});
        server.AddDocument(1, "пушистый кот пушистый хвост"s,       DocumentStatus::ACTUAL, {7, 2, 7});
        server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        server.AddDocument(3, "ухоженный скворец евгений"s,         DocumentStatus::BANNED, {9});
        
        const auto matched_documents = server.FindTopDocuments("пушистый ухоженный кот"s);
        
        bool sorted_array = is_sorted(matched_documents.begin(), matched_documents.end(), 
                         [](const Document& lhs, const Document& rhs) {
            return lhs.relevance > rhs.relevance;
        });
        ASSERT_HINT(sorted_array, "The array should be sorted in descending order"s);
    }
}

//  Checking the accuracy of calculating the average rating
void TestRating() {
    {
        SearchServer server;
        server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        
        //  Calculating the average rating manually
        vector<int> ratings = {5, -12, 2, 1};
        double average_rating;
        if (ratings.empty()) {
            average_rating = 0;
        } else {
            int sum = 0;
            for (const auto& digit: ratings) {
                sum += digit; 
            }
            average_rating = sum / static_cast<int>(ratings.size());
        } // End of calculation
        
        const auto document = server.FindTopDocuments("пёс"s);
        
        ASSERT_EQUAL(document[0].rating, average_rating);
    }
}

void TestPredicate() {
    {
        SearchServer server;
        server.AddDocument(2, "пушистый кот пушистый хвост"s,       DocumentStatus::ACTUAL, {7, 2, 7});
        const auto predicate_check = server.FindTopDocuments("пушистый ухоженный кот"s, [](int document_id, [[maybe_unused]]DocumentStatus status, [[maybe_unused]]int rating)
                                { return document_id % 2 == 0; });
        ASSERT(predicate_check[0].id % 2 == 0);
    }
}

// 
void TestStatus() {
    {
        SearchServer server;
        server.AddDocument(2, "пушистый кот пушистый хвост"s,       DocumentStatus::BANNED, {7, 2, 7});
        const auto check = server.FindTopDocuments("пушистый ухоженный кот"s, DocumentStatus::BANNED);
        ASSERT(check.size() == 1);
    }
}
    
void TestRelevance() {
    {
        SearchServer server;
         server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});
        server.AddDocument(1, "пушистый кот пушистый хвост"s,       DocumentStatus::ACTUAL, {7, 2, 7});
        server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        const auto document = server.FindTopDocuments("пушистый ухоженный кот"s);
        double relevance = log((3*1.0) / 1) * 0.5 + log((3 * 1.0) / 2) * 0.25;
        ASSERT_EQUAL(document[0].relevance, relevance);
    }
}

template <typename T>
void RunTestImpl(T& func, const string& func_name) {
    func();
    cerr << func_name << " OK" << endl;
}

#define RUN_TEST(func)  RunTestImpl(func, #func);


// Function TestSearchServer is the entry point for running tests
void TestSearchServer() {
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    RUN_TEST(TestAddingDocuments);
    RUN_TEST(TestMatching);
    RUN_TEST(TestRelevanceSort);
    RUN_TEST(TestRating);
    RUN_TEST(TestPredicate);
    RUN_TEST(TestStatus);
    RUN_TEST(TestRelevance);
}
// --------- The end of the search engine unit tests -----------

int main() {
    RUN_TEST(TestSearchServer);
    // If you see this line, it means that all tests were successful
    cout << "Search server testing finished"s << endl;
}
