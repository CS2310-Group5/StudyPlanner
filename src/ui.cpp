#include "ui.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <utility>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
bool colorEnabled() {
    if (std::getenv("NO_COLOR")) return false;
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0 &&
           (std::getenv("WT_SESSION") || std::getenv("ANSICON") || std::getenv("TERM"));
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

std::string color(const std::string &text, const char *code) {
    if (!colorEnabled()) return text;
    return std::string("\033[") + code + "m" + text + "\033[0m";
}

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return text;
}

bool removeSensitiveFiles() {
    bool success=true;
    for (const auto *path : {"session_state.json", "page_text.txt", "spark_template.xlsx"}) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) {
            std::cerr << "Could not delete " << path << ": " << ec.message() << "\n";
            success=false;
        }
    }
    return success;
}

bool cleanNonSensitiveFiles() {
    bool success=true;
    const std::filesystem::path cacheDir=".cityu_cache";
    std::error_code ec;
    if(std::filesystem::exists(cacheDir,ec)){
        for(std::filesystem::directory_iterator it(cacheDir,ec),end;!ec&&it!=end;it.increment(ec)){
            if(it->path().filename()=="catalogue-cooldown-until")continue;
            std::error_code removeError;
            std::filesystem::remove_all(it->path(),removeError);
            if(removeError){
                std::cerr<<"Could not remove "<<it->path().string()<<": "<<removeError.message()<<"\n";
                success=false;
            }
        }
        if(ec){
            std::cerr<<"Could not inspect "<<cacheDir.string()<<": "<<ec.message()<<"\n";
            success=false;
        }
    }
    for (const auto *path : {".cityu_logs", "overrides.json"}) {
        std::error_code removeError;
        std::filesystem::remove_all(path, removeError);
        if (removeError) {
            std::cerr << "Could not remove " << path << ": " << removeError.message() << "\n";
            success=false;
        }
    }
    return success;
}

bool confirm(const std::string &prompt) {
    while (true) {
        std::cout << prompt << " (y/n): ";
        std::string answer;
        if (!std::getline(std::cin, answer)) return false;
        answer = upper(trim(answer));
        if (answer == "Y" || answer == "YES") return true;
        if (answer == "N" || answer == "NO" || answer.empty()) return false;
        std::cout << "Please enter Y or N.\n";
    }
}
}

void showStartScreen() {
    std::cout << color("┌──────────────────────────────────────────────────────────┐", "36") << "\n";
    std::cout << color("│                CITYU STUDY PLAN GENERATOR                │", "1;36") << "\n";
    std::cout << color("└──────────────────────────────────────────────────────────┘", "36") << "\n";
    std::cout << "Creates a study-plan workbook from Degree Works and CityU course data.\n\n";
    printInfo("Privacy: your password is never stored; session and study files stay in your account's application data folder.");
    std::cout << "Type DELETE to remove saved session and study files; Q asks before quitting and deleting them.\n";
    std::cout << "CLEAN removes cached pages, logs, and URL overrides; the catalogue cooldown is kept.\n\n";
}

void printSection(const std::string &title) {
    std::cout << "\n\n" << color("◆ ", "36") << color(title, "1;36") << "\n\n";
}

void printInfo(const std::string &message) {
    std::cout << color("INFO  ", "36") << message << "\n";
}

void printSuccess(const std::string &message) {
    std::cout << color("DONE  ", "32") << message << "\n";
}

void printWarning(const std::string &message) {
    std::cout << color("NOTE  ", "33") << message << "\n";
}

bool privacyConfirm() {
    printSection("Your privacy");
    std::cout << "The browser is used only to read your Degree Works page.\n"
                 "Your password is not stored. Press Enter or type OK to continue; C cancels.\n";
    while (true) {
        const std::string answer = upper(readPrompt("Press Enter or type OK to continue"));
        if (answer.empty() || answer == "OK") return true;
        if (answer == "C") return false;
        printWarning("Press Enter or type OK to continue, or use C to cancel.");
    }
}

std::string readPrompt(const std::string &prompt) {
    while (true) {
        std::cout << color(prompt, "1") << "\n> ";
        std::string typed;
        if (!std::getline(std::cin, typed)) {
            std::cerr << "\nInput closed. Exiting without deleting stored data.\n";
            std::exit(130);
        }
        typed = trim(typed);
        const std::string command = upper(typed);

        if (command == "DELETE") {
            if (confirm("Delete saved session and study files?")) {
                if(removeSensitiveFiles())
                    printSuccess("Saved session and study files were deleted.");
                else
                    printWarning("Some saved files could not be deleted; see the error above.");
                std::exit(130);
            }
            continue;
        }
        if (command == "Q") {
            if (confirm("Quit and delete saved session and study files?")) {
                if(removeSensitiveFiles())
                    printSuccess("Saved session and study files were deleted.");
                else
                    printWarning("Some saved files could not be deleted; see the error above.");
                std::exit(130);
            }
            continue;
        }
        if (command == "CLEAN") {
            if (confirm("Remove cached pages, logs, and URL overrides? The catalogue cooldown will remain.")) {
                if(cleanNonSensitiveFiles())
                    printSuccess("Cached pages, logs, and URL overrides were removed; the catalogue cooldown was kept.");
                else
                    printWarning("Some cached data could not be removed; see the error above.");
            }
            continue;
        }
        return typed;
    }
}

bool yesNo(const std::string &prompt) {
    while (true) {
        const std::string answer = upper(readPrompt(prompt + " (Y/N)"));
        if (answer == "Y" || answer == "YES" || answer == "OK") return true;
        if (answer == "N" || answer == "NO") return false;
        printWarning("Please enter Y or N.");
    }
}

MultiSelectAnswer readMultiSelect(const std::string &question,
                                  const std::vector<std::string> &options,
                                  bool allowPrevious) {
    while (true) {
        printSection(question);
        std::cout << "Choose what you do NOT prefer. Select one or more letters separated by commas.\n";
        for (size_t i = 0; i < options.size(); ++i)
            std::cout << "  " << static_cast<char>('A' + i) << ") " << options[i] << "\n";
        if (allowPrevious) std::cout << "  P) Go back and change the previous answer\n";

        const std::string input = upper(readPrompt("Your selection"));
        if (input == "P") {
            if (allowPrevious) return {{}, true};
            printWarning("There is no previous question here.");
            continue;
        }

        std::vector<std::string> selected;
        std::istringstream stream(input);
        std::string token;
        bool invalid = false;
        bool allFine = false;
        while (std::getline(stream, token, ',')) {
            token = trim(token);
            if (token.size() != 1 || token[0] < 'A' ||
                static_cast<size_t>(token[0] - 'A') >= options.size()) {
                invalid = true;
                break;
            }
            const std::string &value = options[static_cast<size_t>(token[0] - 'A')];
            if (std::find(selected.begin(), selected.end(), value) == selected.end())
                selected.push_back(value);
            if (value.find("all fine") != std::string::npos) allFine = true;
        }
        if (invalid || selected.empty()) {
            printWarning("Enter at least one valid option letter.");
            continue;
        }
        if (allFine && selected.size() > 1) {
            printWarning("\"None / all fine\" cannot be combined with another choice.");
            continue;
        }
        return {std::move(selected), false};
    }
}
