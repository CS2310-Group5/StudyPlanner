#pragma once
#include <string>
#include <vector>
#include "models.h"

std::string trimJson(const std::string &s);
bool loadCoursesDb(const std::string &path,
                   std::unordered_map<std::string, Course> &courses,
                   std::string &error);
bool loadGatewayEducationAreas(const std::string &path,
                               std::unordered_map<std::string,std::string> &areas,
                               std::string &error);
bool mergeBundledCourseDb(const std::string &bundledPath,
                          const std::string &personalPath,
                          std::string &error);
bool saveCoursesDb(const std::string &path,
                   const std::unordered_map<std::string, Course> &courses,
                   std::string &error);
