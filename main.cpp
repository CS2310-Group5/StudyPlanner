#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Course {
    std::string code;
    int credits = 0;
    std::string academicYear;
    std::string offeredTerms;
    std::string precursorOptions;
};

std::vector<Course> loadCourses(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open course catalogue: " + path);
    }

    std::string line;
    std::getline(input, line);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        line.erase(0, 3);
    }
    if (line != "course_code,credits,academic_year,offered_terms,precursor_options") {
        throw std::runtime_error("Unexpected course catalogue header in " + path);
    }

    std::vector<Course> courses;
    int lineNumber = 1;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        Course course;
        std::string credits;
        std::istringstream row(line);
        if (!std::getline(row, course.code, ',') ||
            !std::getline(row, credits, ',') ||
            !std::getline(row, course.academicYear, ',') ||
            !std::getline(row, course.offeredTerms, ',') || row.eof()) {
            throw std::runtime_error("Expected five columns on line " + std::to_string(lineNumber));
        }
        std::getline(row, course.precursorOptions);
        if (course.precursorOptions.find(',') != std::string::npos ||
            course.code.empty() || course.academicYear.empty() || course.offeredTerms.empty()) {
            throw std::runtime_error("Invalid course fields on line " + std::to_string(lineNumber));
        }

        const char* end = credits.data() + credits.size();
        const auto parsed = std::from_chars(credits.data(), end, course.credits);
        if (parsed.ec != std::errc{} || parsed.ptr != end || course.credits < 0) {
            throw std::runtime_error("Invalid credits on line " + std::to_string(lineNumber));
        }
        courses.push_back(course);
    }

    if (input.bad()) {
        throw std::runtime_error("Error reading course catalogue: " + path);
    }
    return courses;
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: study-planner [courses.csv]\n";
        return 1;
    }

    try {
        const auto courses = loadCourses(argc == 2 ? argv[1] : "data/courses.csv");
        std::cout << "Loaded " << courses.size() << " course(s).\n";
        for (const auto& course : courses) {
            std::cout << course.code << " | " << course.credits << " credits | "
                      << course.academicYear << " | offered: " << course.offeredTerms
                      << " | precursor options: "
                      << (course.precursorOptions.empty() ? "none" : course.precursorOptions)
                      << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
