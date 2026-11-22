#include "electives.h"
#include "planner.h"
#include "ui.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>
#include <unordered_set>

namespace {
std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool hasText(const std::string &text, const std::string &term) {
    return !term.empty() &&
        lowercase(text).find(lowercase(term)) != std::string::npos;
}

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

bool parsePositiveInteger(const std::string &value, int &result) {
    try {
        size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != value.size() || parsed <= 0) return false;
        result = parsed;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

std::string prerequisiteSummary(const Course &course,
                                const DegreeRequirements &requirements) {
    if (course.prerequisite_source == "UNKNOWN" ||
        std::find(course.prerequisites.begin(), course.prerequisites.end(),
                  "UNKNOWN") != course.prerequisites.end())
        return {};
    if (course.prerequisites.empty())
        return course.prerequisites_text.empty()
            ? "Prerequisites: None"
            : "Prerequisites: " + course.prerequisites_text;

    const std::unordered_set<std::string> completed(
        requirements.completed_courses.begin(),
        requirements.completed_courses.end());
    const std::unordered_set<std::string> studying(
        requirements.in_progress.begin(), requirements.in_progress.end());
    std::unordered_set<std::string> planned(
        requirements.required_courses.begin(),
        requirements.required_courses.end());
    planned.insert(requirements.major_courses.begin(),
                   requirements.major_courses.end());
    for (const auto &group : requirements.choice_groups)
        planned.insert(group.candidates.begin(), group.candidates.end());

    std::string result = "Prerequisites: ";
    for (size_t i = 0; i < course.prerequisites.size(); ++i) {
        if (i) result += ", ";
        const auto &code = course.prerequisites[i];
        result += code + " ";
        if (completed.count(code)) result += "✅ studied";
        else if (studying.count(code)) result += "🔄 studying";
        else if (planned.count(code)) result += "📅 planned";
        else result += "⚠️ not listed";
    }
    return result;
}
}

int electiveCredits(const Course &course) {
    return course.credits_known ? course.credits : 3;
}

bool courseMatchesArea(const Course &course, const std::vector<int> &areas) {
    const auto matches = [](const std::string &value, int area) {
        std::string normalized = lowercase(value);
        normalized.erase(std::remove_if(normalized.begin(), normalized.end(),
            [](unsigned char ch) { return std::isspace(ch); }), normalized.end());
        return normalized.find("area" + std::to_string(area)) != std::string::npos;
    };
    for (int area : areas) {
        if (matches(course.area, area)) return true;
        for (const auto &tag : course.tags)
            if (matches(tag, area)) return true;
    }
    return false;
}

int courseArea(const Course &course) {
    for (int area = 1; area <= 9; ++area)
        if (courseMatchesArea(course, {area})) return area;
    return 0;
}

int candidateScore(const Course &course,
                   const std::vector<std::vector<std::string>> &preferences) {
    int score = 10000;
    switch (course.prereq_status) {
        case PrereqStatus::SATISFIED: score += 4000; break;
        case PrereqStatus::SCHEDULED: score += 3000; break;
        case PrereqStatus::UNKNOWN: score += 2000; break;
        case PrereqStatus::UNSCHEDULED: score += 1000; break;
    }

    if (!preferences.empty()) {
        for (const auto &disliked : preferences[0]) {
            if (disliked.find("all fine") != std::string::npos) continue;
            bool matches = hasText(course.area, disliked);
            for (const auto &tag : course.tags)
                matches = matches || hasText(tag, disliked);
            if (matches) {
                score -= 100;
                break;
            }
        }
    }

    if (preferences.size() > 1) {
        std::string assessmentText = course.assessment.raw_text;
        for (const auto &item : course.assessment.raw_items)
            assessmentText += " " + item;
        for (const auto &tag : course.tags)
            assessmentText += " " + tag;
        for (const auto &disliked : preferences[1]) {
            if (disliked.find("all fine") != std::string::npos) continue;
            const bool matches = disliked == "100% continuous assessment"
                ? is100PercentContinuousAssessment(course.assessment)
                : hasText(assessmentText, disliked);
            if (matches) score -= 10;
        }
    }
    return score;
}

std::vector<size_t> orderChoiceGroups(const std::vector<ChoiceGroup> &groups) {
    std::vector<size_t> order(groups.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&groups](size_t a, size_t b) {
        return groups[a].candidates.size() < groups[b].candidates.size();
    });
    return order;
}

std::string validateChoiceSelection(
    const ChoiceGroup &group,
    const std::vector<std::string> &selected,
    const std::unordered_map<std::string, Course> &courses) {
    if (selected.empty()) return "Select at least one course.";
    std::set<std::string> unique(selected.begin(), selected.end());
    if (unique.size() != selected.size()) return "Select each course only once.";
    if (!group.candidate_sets.empty()) {
        const bool onePath = std::any_of(group.candidate_sets.begin(),
            group.candidate_sets.end(), [&selected](const auto &candidateSet) {
                return std::all_of(selected.begin(), selected.end(),
                    [&candidateSet](const std::string &code) {
                        return std::find(candidateSet.begin(), candidateSet.end(),
                                         code) != candidateSet.end();
                    });
            });
        if (!onePath)
            return "Choose all courses from the same language option.";
    }
    if (group.choose > 0 &&
        selected.size() != static_cast<size_t>(group.choose))
        return "Select exactly " + std::to_string(group.choose) + " courses.";

    int totalCredits = 0;
    std::unordered_map<int, int> areaCredits;
    for (const auto &code : selected) {
        const auto course = courses.find(code);
        if (course == courses.end())
            return "Course details are unavailable for " + code + ".";
        const int credits = electiveCredits(course->second);
        totalCredits += credits;
        const int area = courseArea(course->second);
        if (!group.areas.empty() && !courseMatchesArea(course->second, group.areas))
            return "Choose courses from the listed Degree Works areas.";
        if (area > 0) areaCredits[area] += credits;
    }
    if (group.required_credits > 0 && totalCredits != group.required_credits)
        return "The selected courses total " + std::to_string(totalCredits) +
            " credits; exactly " + std::to_string(group.required_credits) +
            " are required (3 credits assumed where missing).";

    if (group.minimum_per_area_credits > 0 && !group.areas.empty()) {
        const int requiredAreas = (group.required_credits +
            group.minimum_per_area_credits - 1) / group.minimum_per_area_credits;
        const int metAreas = static_cast<int>(std::count_if(
            areaCredits.begin(), areaCredits.end(), [&group](const auto &entry) {
                return entry.second >= group.minimum_per_area_credits &&
                    std::find(group.areas.begin(), group.areas.end(), entry.first) !=
                        group.areas.end();
            }));
        if (metAreas < requiredAreas)
            return "The selection must include at least " +
                std::to_string(group.minimum_per_area_credits) +
                " credits from each of " + std::to_string(requiredAreas) +
                " different listed areas.";
    }
    if (!canSatisfyChoiceGroup(group, selected, courses))
        return "The selected courses do not satisfy the Degree Works credit, count, or area requirement.";
    return {};
}

ElectivePickerResult selectElectives(
    const DegreeRequirements &requirements,
    const std::unordered_map<std::string, Course> &courses,
    std::unordered_map<std::string, Course> &chosen,
    const std::vector<std::vector<std::string>> &eligible_groups,
    const std::vector<bool> &feasible_groups,
    const std::vector<std::vector<std::string>> &preferences,
    bool illustrative) {
    ElectivePickerResult result;
    result.ranked_groups.resize(requirements.choice_groups.size());
    result.selections.reserve(requirements.choice_groups.size());
    const auto order = orderChoiceGroups(requirements.choice_groups);

    for (size_t orderIndex = 0; orderIndex < order.size(); ++orderIndex) {
        const size_t groupIndex = order[orderIndex];
        const auto &group = requirements.choice_groups[groupIndex];
        ElectiveSelection selection;
        selection.requirement = group.required_credits > 0
            ? "Select " + std::to_string(group.required_credits) + " credit units"
            : "Select " + std::to_string(group.choose) + " course(s)";
        if (!group.purpose.empty())
            selection.requirement += " - " + group.purpose;
        if (illustrative)
            selection.requirement =
                "Catalogue sample only; not a degree requirement";
        if (!feasible_groups[groupIndex]) {
            selection.note =
                "Not selected: verified catalogue data is insufficient to complete this Degree Works choice.";
            result.selections.push_back(std::move(selection));
            continue;
        }

        const std::string sectionName = !group.purpose.empty()
            ? group.purpose
            : "Degree Works choice";
        printSection("Degree Works choice " + std::to_string(orderIndex + 1) +
                     " — " + sectionName);
        std::vector<std::string> ranked;
        for (const auto &code : eligible_groups[groupIndex])
            if (!chosen.count(code)) ranked.push_back(code);
        if (!canSatisfyChoiceGroup(group, ranked, courses)) {
            selection.note =
                "Not selected: the remaining verified candidates cannot complete this choice.";
            printWarning(selection.note);
            result.selections.push_back(std::move(selection));
            continue;
        }
        if (group.required_credits > 0)
            printInfo("Degree Works asks you to choose " +
                std::to_string(group.required_credits) +
                " credits here. This choice fills that audit requirement; it is not an extra elective requirement.");
        else
            printInfo("Degree Works asks you to choose " +
                std::to_string(group.choose) +
                " course(s) here. This choice fills that audit requirement; it is not an extra elective requirement.");
        if (!group.areas.empty()) {
            std::cout << "  Area requirement: ";
            for (size_t i = 0; i < group.areas.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << "Area " << group.areas[i];
            }
            if (group.minimum_per_area_credits > 0) {
                const int requiredAreas = (group.required_credits +
                    group.minimum_per_area_credits - 1) /
                    group.minimum_per_area_credits;
                std::cout << " (at least " << group.minimum_per_area_credits
                    << " credits in each of " << requiredAreas
                    << " different areas)";
            }
            std::cout << "\n";
        }
        if (!group.candidate_sets.empty())
            printInfo("Choose courses from one language option only; do not mix options.");
        printInfo("Candidates are ranked by your preferences and prerequisite readiness. Missing credits default to 3.");
        if (group.choose > static_cast<int>(ranked.size())) {
            selection.note =
                "Not selected: fewer verified candidates are available than Degree Works requires.";
            printWarning(selection.note);
            result.selections.push_back(std::move(selection));
            continue;
        }
        std::stable_sort(ranked.begin(), ranked.end(),
            [&courses, &preferences](const std::string &a,
                                     const std::string &b) {
                const auto ai = courses.find(a);
                const auto bi = courses.find(b);
                const int aScore = ai == courses.end()
                    ? 0 : candidateScore(ai->second, preferences);
                const int bScore = bi == courses.end()
                    ? 0 : candidateScore(bi->second, preferences);
                return aScore > bScore;
            });
        result.ranked_groups[groupIndex] = ranked;

        if (illustrative) {
            int selectedCredits = 0;
            int selectedCount = 0;
            for (const auto &code : ranked) {
                if (group.choose > 0 &&
                    selectedCount >= group.choose) break;
                const Course &course = courses.at(code);
                if (!group.areas.empty() &&
                    !courseMatchesArea(course, group.areas)) continue;
                if (group.required_credits > 0 &&
                    selectedCredits + electiveCredits(course) >
                        group.required_credits) continue;
                chosen[code] = course;
                selection.course_codes.push_back(code);
                selectedCredits += electiveCredits(course);
                ++selectedCount;
                if (group.required_credits > 0 &&
                    selectedCredits == group.required_credits) break;
            }
            if ((group.choose > 0 &&
                 selectedCount != group.choose) ||
                (group.required_credits > 0 &&
                 selectedCredits != group.required_credits)) {
                for (const auto &code : selection.course_codes)
                    chosen.erase(code);
                selection.course_codes.clear();
                selection.note =
                    "Not selected: bundled course data could not complete this example choice.";
                printWarning(selection.note);
            } else {
                for (const auto &code : selection.course_codes) {
                    const auto &course = courses.at(code);
                    printSuccess("Elective selected: " + code + " — " +
                        course.title + " (" +
                        std::to_string(electiveCredits(course)) +
                        " credits" +
                        (course.credits_known ? ")" : ", assumed)"));
                }
            }
            result.selections.push_back(std::move(selection));
            continue;
        }

        printInfo("Ranked courses");
        std::vector<size_t> displayOrder;
        std::vector<int> displayAreas;
        displayOrder.reserve(ranked.size());
        displayAreas.reserve(ranked.size());
        if (group.areas.empty()) {
            for (size_t i = 0; i < ranked.size(); ++i) {
                displayOrder.push_back(i);
                displayAreas.push_back(0);
            }
        } else {
            std::unordered_set<std::string> displayed;
            for (int area : group.areas) {
                for (size_t i = 0; i < ranked.size(); ++i) {
                    const auto course = courses.find(ranked[i]);
                    if (course != courses.end() &&
                        courseMatchesArea(course->second, {area}) &&
                        displayed.insert(ranked[i]).second) {
                        displayOrder.push_back(i);
                        displayAreas.push_back(area);
                    }
                }
            }
        }
        const auto printCandidate = [&](size_t row) {
            const auto &code = ranked[displayOrder[row]];
            const auto it = courses.find(code);
            std::cout << "  " << row + 1 << ") " << code;
            if (it != courses.end() && !it->second.title.empty())
                std::cout << " — " << it->second.title;
            if (it != courses.end())
                std::cout << " | " << electiveCredits(it->second)
                    << " credits" << (it->second.credits_known
                        ? "" : " (assumed)");
            std::cout << "\n";
            if (it != courses.end()) {
                const int area = courseArea(it->second);
                if (area > 0) std::cout << "     Area " << area << "\n";
                const std::string prerequisites =
                    prerequisiteSummary(it->second, requirements);
                if (!prerequisites.empty())
                    std::cout << "     " << prerequisites << "\n";
                if (!it->second.semesters.empty()) {
                    std::cout << "     Offering: ";
                    for (size_t term = 0;
                         term < it->second.semesters.size(); ++term) {
                        if (term) std::cout << ", ";
                        std::cout << it->second.semesters[term];
                    }
                    if (std::find(it->second.semesters.begin(),
                                  it->second.semesters.end(),
                                  "Not offered") !=
                        it->second.semesters.end())
                        std::cout << " (provisionally treated as Semester A/B)";
                    std::cout << "\n";
                }
                if (it->second.assessment.exam_percent &&
                    it->second.assessment.other_percent)
                    std::cout << "     Assessment: Exam "
                        << *it->second.assessment.exam_percent
                        << "%; coursework "
                        << *it->second.assessment.other_percent << "%\n";
                const std::string url = it->second.url.empty()
                    ? "https://www.cityu.edu.hk/catalogue/ug/current/course/" +
                        code + ".htm"
                    : it->second.url;
                std::cout << "     " << url << "\n";
            } else {
                std::cout << "     https://www.cityu.edu.hk/catalogue/ug/current/course/"
                    << code << ".htm\n";
            }
            std::cout << "\n     ─────────────────────────────────────────\n\n";
        };
        const auto printCandidatePage = [&](size_t begin, size_t end) {
            if (group.areas.empty()) {
                for (size_t row = begin; row < end; ++row)
                    printCandidate(row);
                return;
            }
            for (int area : group.areas) {
                printSection("Area " + std::to_string(area));
                bool found = false;
                for (size_t row = begin; row < end; ++row) {
                    if (displayAreas[row] == area) {
                        printCandidate(row);
                        found = true;
                    }
                }
                if (!found)
                    printInfo("No verified candidates in this area on the current page.");
            }
        };
        constexpr size_t pageSize = 20;
        const auto pageEnd = [&](size_t begin) {
            size_t end = std::min(displayOrder.size(), begin + pageSize);
            if (end >= displayOrder.size()) return displayOrder.size();
            const auto current =
                courses.find(ranked[displayOrder[end - 1]]);
            if (current == courses.end()) return end;
            const int boundaryScore =
                candidateScore(current->second, preferences);
            while (end < displayOrder.size()) {
                const auto next =
                    courses.find(ranked[displayOrder[end]]);
                if (next == courses.end() ||
                    candidateScore(next->second, preferences) != boundaryScore)
                    break;
                ++end;
            }
            return end;
        };
        size_t visibleCount = pageEnd(0);
        printCandidatePage(0, visibleCount);
        while (visibleCount < displayOrder.size()) {
            const size_t nextPageEnd = pageEnd(visibleCount);
            printInfo("Showing courses 1–" +
                std::to_string(visibleCount) + " of " +
                std::to_string(displayOrder.size()) + ".");
            const std::string more = readPrompt(
                "Type M for the next page, or press Enter to choose");
            if (uppercase(more) != "M") break;
            printCandidatePage(visibleCount, nextPageEnd);
            visibleCount = nextPageEnd;
        }
        if (group.choose > 0 &&
            displayOrder.size() < static_cast<size_t>(group.choose))
            printWarning("Fewer candidates are available than the required course count.");

        while (true) {
            const std::string requirement = group.required_credits > 0
                ? "select courses totaling exactly " +
                    std::to_string(group.required_credits) + " credits"
                : "select exactly " + std::to_string(group.choose) +
                    " course(s)";
            const std::string picks = readPrompt(
                requirement +
                "; enter the numbers above, separated by commas");
            std::vector<int> ids;
            bool bad = false;
            std::istringstream pickStream(picks);
            std::string token;
            while (std::getline(pickStream, token, ',')) {
                const size_t first = token.find_first_not_of(" \t");
                if (first == std::string::npos) token.clear();
                else token.erase(0, first);
                while (!token.empty() &&
                       std::isspace(static_cast<unsigned char>(token.back())))
                    token.pop_back();
                int row = 0;
                if (!parsePositiveInteger(token, row)) bad = true;
                else ids.push_back(row - 1);
            }
            std::sort(ids.begin(), ids.end());
            std::string error;
            if (bad || ids.empty())
                error = "Enter distinct, valid course numbers separated by commas.";
            else if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
                error = "Select each course only once.";
            else if (ids.front() < 0 ||
                     static_cast<size_t>(ids.back()) >= visibleCount)
                error = "One or more course numbers are not shown above. Type M to view more courses.";
            std::vector<std::string> selected;
            if (error.empty()) {
                selected.reserve(ids.size());
                for (int id : ids)
                    selected.push_back(
                        ranked[displayOrder[static_cast<size_t>(id)]]);
                error = validateChoiceSelection(group, selected, courses);
            }
            if (!error.empty()) {
                printWarning(error);
                continue;
            }
            std::cout << "✅ Selected:";
            for (int id : ids) {
                const auto &code =
                    ranked[displayOrder[static_cast<size_t>(id)]];
                std::cout << " " << code;
                chosen[code] = courses.at(code);
                selection.course_codes.push_back(code);
            }
            std::cout << "\n";
            break;
        }
        result.selections.push_back(std::move(selection));
    }
    return result;
}
