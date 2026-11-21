#pragma once

#include "planner.h"
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

struct ElectivePickerResult {
    std::vector<ElectiveSelection> selections;
    std::vector<std::vector<std::string>> ranked_groups;
};

int electiveCredits(const Course &course);
bool courseMatchesArea(const Course &course, const std::vector<int> &areas);
int courseArea(const Course &course);
int candidateScore(const Course &course,
                   const std::vector<std::vector<std::string>> &preferences);
std::vector<size_t> orderChoiceGroups(const std::vector<ChoiceGroup> &groups);
std::string validateChoiceSelection(
    const ChoiceGroup &group,
    const std::vector<std::string> &selected,
    const std::unordered_map<std::string, Course> &courses);
ElectivePickerResult selectElectives(
    const DegreeRequirements &requirements,
    const std::unordered_map<std::string, Course> &courses,
    std::unordered_map<std::string, Course> &chosen,
    const std::vector<std::vector<std::string>> &eligible_groups,
    const std::vector<bool> &feasible_groups,
    const std::vector<std::vector<std::string>> &preferences,
    bool illustrative);
