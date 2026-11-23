#pragma once
#include <string>

struct Config {
    int min_request_interval_ms = 60000;
    int jitter_ms = 30000;
    int max_retries = 3;
    int cache_ttl_hours = 720;
    int block_cooldown_hours = 24;
    int elective_top_limit = 20;
    int default_max_credits = 24;
    int advanced_standing_max_credits = 21;
};
bool loadConfig(const std::string &path, Config &config, std::string &error);
