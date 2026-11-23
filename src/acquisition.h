#pragma once
#include "models.h"
#include "config.h"
#include <unordered_map>
#include <string>
#include <chrono>
#include <vector>
#include <cstddef>

enum class AcquisitionMode { STATIC_FIRST, FULL_DYNAMIC };

Assessment parseCourseAssessment(const std::string &html);
std::vector<std::string> parseCourseOfferingTerms(const std::string &html);
Course parseCoursePageHtml(const std::string &code,const std::string &url,
                           const std::string &html);

struct AcquisitionResult {
    Course course;
    bool needs_fix = false;
    bool site_blocked = false;
    std::string error_code;
    std::string plain_error;
    std::string database_save_error;
};

class Acquisition {
public:
    Acquisition(const Config &config, AcquisitionMode mode);
    bool loadStaticDb(const std::string &path, std::string &error);
    bool loadFallbackDb(const std::string &path, std::string &error);
    AcquisitionResult fetch(const std::string &code, bool refresh);
    std::size_t pendingNetworkFetches(const std::vector<std::string> &codes,
                                      bool refresh) const;
    std::vector<std::string> unavailableWithoutNetwork(
        const std::vector<std::string> &codes, bool refresh) const;
    bool catalogueCoolingDown() const { return site_blocked_; }
    long long cooldownRemainingSeconds() const;
    const std::unordered_map<std::string,Course>& staticDb() const {return db_;}
private:
    Config config_;
    AcquisitionMode mode_;
    std::unordered_map<std::string,Course> db_;
    std::unordered_map<std::string,Course> fallback_db_;
    std::unordered_map<std::string,std::string> overrides_;
    std::string static_db_path_ = "courses_db.json";
    bool site_blocked_ = false;
    long long blocked_until_epoch_ = 0;
    std::chrono::steady_clock::time_point last_request_{};
    bool has_requested_ = false;
    void rememberBlock();
    Course dynamicFetch(const std::string &code, const std::string &overrideUrl,
                        bool refresh, bool &ok, std::string &error);
};
