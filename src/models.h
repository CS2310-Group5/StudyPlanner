#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class PrereqStatus { SATISFIED, SCHEDULED, UNSCHEDULED, UNKNOWN };

struct Assessment {
    std::optional<int> exam_percent;
    std::optional<int> other_percent;
    std::vector<std::string> raw_items;
    std::string raw_text;
    bool unknown = true;
};

inline bool is100PercentContinuousAssessment(const Assessment &assessment) {
    return !assessment.unknown && assessment.exam_percent &&
        assessment.other_percent && *assessment.exam_percent == 0 &&
        *assessment.other_percent == 100;
}

struct Course {
    std::string code;
    std::string title;
    int credits = 3;
    bool credits_known = false;
    std::vector<std::string> semesters;
    Assessment assessment;
    std::vector<std::string> prerequisites;
    std::string prerequisites_text;
    std::vector<std::string> exclusive_courses;
    std::string catalogue_notes;
    std::string prerequisite_source = "UNKNOWN";
    std::string catalogue_year;
    bool catalogue_detail_available = false;
    std::string area;
    std::vector<std::string> tags;
    std::string url;
    PrereqStatus prereq_status = PrereqStatus::UNKNOWN;
};

Course mergeCourseData(const Course &primary, const Course &fallback);

struct ChoiceGroup {
    int choose = 0;
    int required_credits = 0;
    std::vector<std::string> candidates;
    std::vector<std::vector<std::string>> candidate_sets;
    std::vector<int> areas;
    int minimum_per_area_credits = 0;
    std::string purpose;
};

struct InProgressCourse {
    std::string code;
    std::string title;
    std::string grade;
    int credits = 0;
    bool credits_known = false;
    std::string term;
};

struct DegreeRequirements {
    std::vector<std::string> in_progress;
    std::vector<InProgressCourse> in_progress_details;
    std::vector<std::string> completed_courses;
    std::vector<std::string> required_courses;
    std::vector<std::string> major_courses;
    std::vector<ChoiceGroup> choice_groups;
    std::vector<std::string> non_degree_courses;
    std::unordered_map<std::string,std::string> course_titles;
    std::string student_name;
    std::string student_id;
    std::string programme;
    std::string college;
    std::string degree;
    std::string major;
    std::string cohort;
    std::string department;
    std::string expected_graduation_date;
    std::string audit_date;
    std::string current_semester;
    std::string entry_status;
    int max_credits_permitted = 0;
    int audit_remaining_credits = 0;
    int remaining_years = 0;
    bool conflicting_in_progress_terms = false;
    bool illustrative = false;
};

inline std::string prereqStatusText(PrereqStatus s) {
    switch (s) {
        case PrereqStatus::SATISFIED: return "SATISFIED";
        case PrereqStatus::SCHEDULED: return "SCHEDULED";
        case PrereqStatus::UNSCHEDULED: return "UNSCHEDULED";
        default: return "UNKNOWN";
    }
}
