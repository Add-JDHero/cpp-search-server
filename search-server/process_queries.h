#pragma once
#include "document.h"
#include"search_server.h"

#include <vector>
#include <execution>
#include <algorithm>
#include <string>
#include <list>

std::vector<std::vector<Document>> ProcessQueries(
    const SearchServer& search_server,
    const std::vector<std::string>& queries);

std::list<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const std::vector<std::string>& queries);