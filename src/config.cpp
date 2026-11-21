#include "config.h"
#include <fstream>

bool loadConfig(const std::string &path, Config &config, std::string &error) {
    std::ifstream f(path); if(!f){error="Could not open configuration file: "+path;return false;}
    std::string s((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    auto number=[&](const char* key,int def)->int{
        std::string needle=std::string("\"")+key+"\"";size_t p=s.find(needle);if(p==std::string::npos)return def;
        p=s.find(':',p);if(p==std::string::npos)return def;p++;
        try{return std::stoi(s.substr(p));}catch(...){return def;}
    };
    config.min_request_interval_ms=number("min_request_interval_ms",60000);
    config.jitter_ms=number("jitter_ms",30000);
    config.max_retries=number("max_retries",3);
    config.cache_ttl_hours=number("cache_ttl_hours",720);
    config.block_cooldown_hours=number("block_cooldown_hours",24);
    config.elective_top_limit=number("elective_top_limit",20);
    config.default_max_credits=number("default_max_credits",24);
    config.advanced_standing_max_credits=number("advanced_standing_max_credits",21);
    return true;
}
