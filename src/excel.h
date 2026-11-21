#pragma once
#include "planner.h"
#include <string>

bool exportExcel(const Plan &plan,const std::string &writerPath,
                 const std::string &pythonPath,
                 std::string &output,std::string &error);
