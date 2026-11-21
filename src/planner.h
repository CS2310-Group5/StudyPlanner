#pragma once
#include "models.h"
#include "config.h"
#include <unordered_map>
#include <vector>

struct PlanCourse {
    Course course;
    std::string requirement;
    bool elective_selection = false;
    std::unordered_map<std::string,std::string> prerequisite_statuses;
    std::string year_semester;
    std::string second_year_semester;
    int first_term_credits = 0;
    int second_term_credits = 0;
    bool warning = false;
    std::vector<std::string> warnings;
    bool counts_toward_degree = true;
    bool manual_placement = false;
};

struct ElectiveSelection {
    std::string requirement;
    std::vector<std::string> course_codes;
    std::string note;
};

struct Plan {
    std::vector<PlanCourse> courses;
    std::vector<ElectiveSelection> elective_selections;
    std::vector<std::string> issues;
    std::vector<std::string> warnings;
    bool illustrative = false;
    int plan_years = 4;
    int start_term_index = 0;
    std::string current_semester;
    std::string student_name;
    std::string student_id;
    std::string programme;
    std::string college;
    std::string department;
    std::string degree;
    std::string major;
    std::string cohort;
    std::string expected_graduation_date;
    std::string audit_date;
    int max_credits_permitted = 0;
};

void classifyPrerequisites(DegreeRequirements &req,
                           std::unordered_map<std::string,Course> &courses);
bool hasSchedulableOffering(const Course &course);
bool hasOfferingInTerm(const Course &course, const std::string &term);
bool hasUsableChoiceCourse(const Course &course, bool requiresCredits);
bool canSatisfyChoiceGroup(const ChoiceGroup &group,
                           const std::vector<std::string> &candidates,
                           const std::unordered_map<std::string,Course> &courses);
Plan makePlan(const DegreeRequirements &req,
              const std::unordered_map<std::string,Course> &courses,
              const std::unordered_map<std::string,Course> &chosen,
              const Config &config,
              bool advancedStanding);
