#pragma once
#include <string>

void writeErrorLog(const std::string &code,const std::string &plain,
                   const std::string &action,const std::string &technical);
std::string logPath();
