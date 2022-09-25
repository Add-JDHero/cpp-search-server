#include  "process_queries.h"

std::vector<std::vector<Document>> ProcessQueries(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {

 std::vector<std::vector<Document>> result(queries.size());
    std::transform(std::execution::par,
 queries.begin(), queries.end(),
 result.begin(),
 [&search_server](const std::string buff) {
            return search_server.FindTopDocuments(buff);
        });
    return result;
}

std::list<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {

 std::list<Document> result;
    for (auto step : ProcessQueries(search_server, queries)) {
        for (auto doc : step) {
 result.push_back(doc);
        }
    }
    return result;
}