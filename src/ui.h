#pragma once
#include <string>
#include <vector>

struct MultiSelectAnswer {
    std::vector<std::string> selections;
    bool previous = false;
};

void showStartScreen();
bool privacyConfirm();
bool yesNo(const std::string &prompt);
std::string readPrompt(const std::string &prompt);
MultiSelectAnswer readMultiSelect(const std::string &question,
                                  const std::vector<std::string> &options,
                                  bool allowPrevious);
void printSection(const std::string &title);
void printInfo(const std::string &message);
void printSuccess(const std::string &message);
void printWarning(const std::string &message);
