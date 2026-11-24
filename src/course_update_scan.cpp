#include "course_update_scan.h"
#include "acquisition.h"
#include "json.h"
#include "logging.h"
#include "ui.h"
#include <algorithm>
#include <cctype>
#include <system_error>
#include <regex>

namespace {
std::string durationText(long long seconds) {
    if (seconds < 60) return std::to_string(seconds) + " seconds";
    long long minutes = seconds / 60;
    const long long hours = minutes / 60;
    minutes %= 60;
    if (hours > 0)
        return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
    return std::to_string(minutes) + "m";
}

std::string upperText(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}
}

std::vector<std::string> degreeWorksCourseCodes(
        const DegreeRequirements &requirements) {
    std::unordered_set<std::string> unique;
    const auto add = [&](const std::string &raw) {
        std::string code = upperText(raw);
        code.erase(std::remove_if(code.begin(), code.end(),
            [](unsigned char ch) { return std::isspace(ch); }), code.end());
        static const std::regex courseCode(R"([A-Z]{2,5}[0-9]{4}[A-Z]?)");
        if (std::regex_match(code, courseCode)) unique.insert(code);
    };
    for (const auto &code : requirements.in_progress) add(code);
    for (const auto &code : requirements.completed_courses) add(code);
    for (const auto &code : requirements.required_courses) add(code);
    for (const auto &code : requirements.major_courses) add(code);
    for (const auto &code : requirements.non_degree_courses) add(code);
    for (const auto &group : requirements.choice_groups)
        for (const auto &code : group.candidates) add(code);
    std::vector<std::string> result(unique.begin(), unique.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::unordered_set<std::string> scanDegreeWorksCourseUpdates(
        const std::vector<std::string> &courseCodes,
        const std::filesystem::path &scannedDatabasePath,
        const Config &config,
        std::unordered_map<std::string, Course> &currentCourses,
        const std::unordered_map<std::string, std::string> &gatewayAreas) {
    std::unordered_set<std::string> attemptedCodes;
    printSection("Degree Works course update scan");
    printInfo("Scanning only course codes found in this Degree Works audit; elective selection and workbook generation wait until you continue.");

    std::string databaseError;
    std::error_code pathError;
    bool databaseExists =
        std::filesystem::exists(scannedDatabasePath, pathError);
    if (pathError) {
        printWarning("Could not check the scanned database path; no scan requests were started: " +
                     pathError.message());
        return attemptedCodes;
    }
    if (!databaseExists) {
        const std::unordered_map<std::string, Course> emptyDatabase;
        if (!saveCoursesDb(scannedDatabasePath.string(), emptyDatabase,
                           databaseError))
            printWarning("Could not create the scanned course database; no scan requests were started: " +
                         databaseError);
    }
    pathError.clear();
    databaseExists = std::filesystem::exists(scannedDatabasePath, pathError);

    if (pathError) {
        printWarning("Could not check the scanned database path; no scan requests were started: " +
                     pathError.message());
    } else if (!databaseExists) {
        printWarning("The scanned course database is unavailable; no scan requests were started.");
    } else if (courseCodes.empty()) {
        printInfo("No course codes were found in Degree Works, so there is nothing to scan.");
    } else {
        Acquisition scanner(config, AcquisitionMode::FULL_DYNAMIC);
        if (!scanner.loadStaticDb(scannedDatabasePath.string(), databaseError)) {
            printWarning("The course update scan could not load the local database and made no requests: " +
                         databaseError);
        } else {
            const std::size_t scanCount =
                scanner.pendingNetworkFetches(courseCodes, true);
            printInfo("Unique Degree Works course codes found in audit: " +
          std::to_string(courseCodes.size()) + ".");
const std::size_t alreadyComplete = courseCodes.size() - scanCount;
if (alreadyComplete > 0)
    printInfo(std::to_string(alreadyComplete) +
              " course(s) already have complete local details.");
printInfo(std::to_string(scanCount) +
          " course(s) still need a catalogue refresh.");
            const long long remaining = scanner.cooldownRemainingSeconds();
            if (scanner.catalogueCoolingDown()) {
                printWarning("Catalogue access is cooling down for about " +
                    durationText(remaining) +
                    "; no scan requests were started. Try again after the cooldown.");
            } else if (scanCount == 0) {
                printInfo("No course updates are pending.");
            } else {
                const long long interval =
                    std::max(0, config.min_request_interval_ms);
                const long long jitter = std::max(0, config.jitter_ms);
                const long long oneAttemptMin =
                    static_cast<long long>(scanCount - 1) * interval / 1000;
                const long long oneAttemptMax =
                    static_cast<long long>(scanCount) * 30 +
                    static_cast<long long>(scanCount - 1) *
                        (interval + jitter) / 1000;
                const long long attempts =
                    static_cast<long long>(scanCount) *
                    std::max(1, config.max_retries);
                long long backoff = 0;
                for (int i = 0; i < std::max(0, config.max_retries - 1); ++i)
                    backoff += 1LL << std::min(i, 20);
                const long long retryMax =
                    attempts * 30 +
                    std::max(0LL, attempts - 1) * (interval + jitter) / 1000 +
                    static_cast<long long>(scanCount) * backoff;
                printInfo("Estimated time: about " +
                    durationText(oneAttemptMin) + "–" +
                    durationText(retryMax) +
                    " with configured retries. Maybe take a nap first? The catalogue requests are deliberately unhurried.");
                printInfo("Press Ctrl+C at any time to stop. Each successful course update is saved as it completes.");
               const std::string consent =
            readPrompt("Type OK (or YES) to scan these Degree Works courses");
            const std::string answer = upperText(consent);
            if (answer != "OK" && answer != "YES" && answer != "Y") {
                printWarning("Course update scan cancelled; no scan requests were started.");
            } else {
                    std::size_t updated = 0;
                    std::size_t failed = 0;
                    std::size_t saveFailures = 0;
                    for (size_t index = 0; index < courseCodes.size(); ++index) {
                        const std::string &code = courseCodes[index];
                        attemptedCodes.insert(code);
                        printInfo("Scanning course " +
                            std::to_string(index + 1) + " of " +
                            std::to_string(courseCodes.size()) + ": " + code);
                        AcquisitionResult result = scanner.fetch(code, true);
                        if (!result.database_save_error.empty()) {
                            ++saveFailures;
                            printWarning(code +
                                " was fetched, but its update could not be saved: " +
                                result.database_save_error);
                            writeErrorLog("DB-SAVE", result.database_save_error,
                                "Check write permissions for the course database.",
                                code);
                        }
                        if (result.needs_fix) {
                            ++failed;
                            printWarning(code + " could not be updated: " +
                                result.plain_error);
                            writeErrorLog(result.error_code, result.plain_error,
                                "Check catalogue access and retry the course update scan.",
                                code);
                        } else {
                            ++updated;
                            if (result.database_save_error.empty())
                                printSuccess("Updated course information for " +
                                             code + ".");
                            else
                                printInfo("Fetched updated course information for " +
                                    code +
                                    ", but it is only available in this run because saving failed.");
                        }
                        if (result.site_blocked) {
                            printWarning("The catalogue blocked or challenged the scan; remaining requests were stopped.");
                            break;
                        }
                    }
                    for (const auto &code : attemptedCodes) {
                        const auto refreshed = scanner.staticDb().find(code);
                        if (refreshed == scanner.staticDb().end()) continue;
                        Course primary = refreshed->second;
                        const auto fallback = currentCourses.find(code);
                        if (fallback != currentCourses.end())
                            primary = mergeCourseData(primary, fallback->second);
                        const auto area = gatewayAreas.find(code);
                        if (area != gatewayAreas.end())
                            primary.area = area->second;
                        currentCourses[code] = std::move(primary);
                    }
                    printSection("Course update scan summary");
printInfo("Updated: " + std::to_string(updated) +
    "; update failures: " + std::to_string(failed) +
    "; save failures: " + std::to_string(saveFailures) +
    "; not attempted: " +
    std::to_string(courseCodes.size() -
                   attemptedCodes.size()) + ".");
if (failed > 0 || saveFailures > 0)
    printInfo("You can run the course update scan again later to retry the remaining courses.");
if (scanner.catalogueCoolingDown())
    printWarning("Catalogue access is currently cooling down. Wait before starting another scan.");
                }
            }
        }
    }
    return attemptedCodes;
}
