#pragma once

#include "config.h"
#include "models.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

std::vector<std::string> degreeWorksCourseCodes(
    const DegreeRequirements &requirements);
std::unordered_set<std::string> scanDegreeWorksCourseUpdates(
    const std::vector<std::string> &courseCodes,
    const std::filesystem::path &scannedDatabasePath,
    const Config &config,
    std::unordered_map<std::string, Course> &currentCourses,
    const std::unordered_map<std::string, std::string> &gatewayAreas);
