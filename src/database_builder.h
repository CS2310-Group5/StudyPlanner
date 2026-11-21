#pragma once

#include "config.h"
#include "models.h"
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct PastedCourseDatabase {
    std::unordered_map<std::string,Course> courses;
    std::unordered_set<std::string> detailed_codes;
    std::string catalogue_year;
    std::size_t detail_blocks_seen = 0;
    std::size_t unparsed_detail_blocks = 0;
};

bool parsePastedCourseDatabase(const std::string &text,
                               PastedCourseDatabase &database,
                               std::string &error);
int runDatabaseBuilder(const std::string &sourcePath,bool importOnly,
                       bool fetchOnly,Config config);
