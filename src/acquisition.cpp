#include "acquisition.h"
#include "json.h"
#include <curl/curl.h>
#include <regex>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <random>
#include <cctype>
#include <algorithm>
#include <ctime>
#include <unordered_set>

namespace {
size_t writeCb(void *ptr,size_t size,size_t nm,size_t ud){
    std::string *out=(std::string*)ud;out->append((char*)ptr,size*nm);return size*nm;
}
bool challenge(const std::string&s){
    std::string l=s;for(char&c:l)c=(char)std::tolower((unsigned char)c);
    return l.find("incapsula")!=std::string::npos||l.find("captcha")!=std::string::npos||
           l.find("request unsuccessful")!=std::string::npos||l.find("challenge")!=std::string::npos||
           l.find("access denied")!=std::string::npos||l.find("additional check")!=std::string::npos||
           l.find("verify you are human")!=std::string::npos||
           l.find("incident id")!=std::string::npos;
}
bool cacheIsFresh(const std::string &path,int ttlHours){
    std::error_code ec;
    auto stamp=std::filesystem::last_write_time(path,ec);
    if(ec)return false;
    auto age=std::chrono::duration_cast<std::chrono::hours>(
        std::filesystem::file_time_type::clock::now()-stamp).count();
    return age>=0&&age<ttlHours;
}
bool staticCourseComplete(const Course&course){
    if(course.title.empty()||!course.credits_known||course.semesters.empty())return false;
    if(course.catalogue_detail_available)return true;
    return course.prerequisite_source!="UNKNOWN"&&!course.assessment.unknown;
}
std::string plainText(const std::string &html){
    std::string text=std::regex_replace(html,std::regex("<[^>]*>")," ");
    for(const auto &[entity,replacement]:std::vector<std::pair<std::string,std::string>>{
            {"&nbsp;"," "},{"&#160;"," "},{"&amp;","&"},{"&ndash;","-"},{"&mdash;","-"}}){
        size_t pos=0;while((pos=text.find(entity,pos))!=std::string::npos){
            text.replace(pos,entity.size(),replacement);pos+=replacement.size();
        }
    }
    size_t start=text.find_first_not_of(" \t\r\n");
    if(start==std::string::npos)return {};
    size_t end=text.find_last_not_of(" \t\r\n");
    return text.substr(start,end-start+1);
}
std::string elementText(const std::string &html,const std::string &id){
    const std::regex element(
        R"(<([a-zA-Z][a-zA-Z0-9]*)\b[^>]*\bid\s*=\s*["']([^"']+)["'][^>]*>([\s\S]*?)</\1\s*>)",
        std::regex_constants::icase);
    for(std::sregex_iterator it(html.begin(),html.end(),element),end;it!=end;++it){
        std::string found=(*it)[2].str();
        std::transform(found.begin(),found.end(),found.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        std::string wanted=id;
        std::transform(wanted.begin(),wanted.end(),wanted.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        if(found==wanted)return plainText((*it)[3].str());
    }
    return {};
}
std::string normalizedLabel(std::string text){
    text=plainText(text);
    std::string normalized;
    for(unsigned char ch:text)
        if(std::isalnum(ch))
            normalized+=static_cast<char>(std::tolower(ch));
    return normalized;
}
std::string tableFieldText(const std::string&html,const std::string&label){
    const std::string wanted=normalizedLabel(label);
    const std::regex row(R"(<tr\b[^>]*>([\s\S]*?)</tr\s*>)",
                         std::regex_constants::icase);
    const std::regex cell(R"(<t[dh]\b[^>]*>([\s\S]*?)</t[dh]\s*>)",
                          std::regex_constants::icase);
    for(std::sregex_iterator rowIt(html.begin(),html.end(),row),rowEnd;
        rowIt!=rowEnd;++rowIt){
        std::vector<std::string> cells;
        const std::string rowHtml=(*rowIt)[1].str();
        for(std::sregex_iterator cellIt(rowHtml.begin(),rowHtml.end(),cell),
            cellEnd;cellIt!=cellEnd;++cellIt)
            cells.push_back(plainText((*cellIt)[1].str()));
        if(cells.size()<2)continue;
        const std::string actual=normalizedLabel(cells.front());
        if(actual==wanted||actual.rfind(wanted,0)==0){
            std::string value;
            for(size_t i=1;i<cells.size();++i){
                if(!value.empty())value+=" ";
                value+=cells[i];
            }
            return value;
        }
    }
    return {};
}
std::string courseTitleFromTable(const std::string&html,const std::string&code){
    const std::regex row(R"(<tr\b[^>]*>([\s\S]*?)</tr\s*>)",
                         std::regex_constants::icase);
    const std::regex titlePattern(
        "^\\s*"+code+"\\s*[-:]\\s*(.+?)\\s*$",
        std::regex_constants::icase);
    for(std::sregex_iterator it(html.begin(),html.end(),row),end;it!=end;++it){
        const std::string text=plainText((*it)[1].str());
        std::smatch match;
        if(std::regex_match(text,match,titlePattern))return match[1].str();
    }
    return {};
}
std::vector<std::string> parseExclusiveCourseCodes(const std::string&html,
                                                   const std::string&currentCode){
    std::string value=elementText(html,"div_exclusive_courses");
    if(value.empty()){
        const std::regex row(R"(<tr\b[^>]*>([\s\S]*?)</tr\s*>)",
                             std::regex_constants::icase);
        for(std::sregex_iterator it(html.begin(),html.end(),row),end;it!=end;++it){
            const std::string text=plainText((*it)[1].str());
            std::string normalized=text;
            std::transform(normalized.begin(),normalized.end(),normalized.begin(),
                [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
            if(normalized.find("exclusive courses")!=std::string::npos){
                value=text;
                break;
            }
        }
    }
    const std::regex codePattern(R"(\b[A-Z]{2,5}\s?[0-9]{4}[A-Z]?\b)",
                                 std::regex_constants::icase);
    std::vector<std::string> codes;
    for(std::sregex_iterator it(value.begin(),value.end(),codePattern),end;it!=end;++it){
        std::string code=(*it).str();
        code.erase(std::remove_if(code.begin(),code.end(),
            [](unsigned char ch){return std::isspace(ch);}),code.end());
        std::transform(code.begin(),code.end(),code.begin(),
            [](unsigned char ch){return static_cast<char>(std::toupper(ch));});
        if(code!=currentCode&&std::find(codes.begin(),codes.end(),code)==codes.end())
            codes.push_back(code);
    }
    return codes;
}
}

Acquisition::Acquisition(const Config &config, AcquisitionMode mode):config_(config),mode_(mode){}

Assessment parseCourseAssessment(const std::string &html){
    Assessment assessment;
    std::regex element(
        R"(<([a-zA-Z][a-zA-Z0-9]*)\b[^>]*\bid\s*=\s*["']([^"']*assessment[^"']*)["'][^>]*>([\s\S]*?)</\1\s*>)",
        std::regex_constants::icase);
    std::regex percent(R"(([0-9]{1,3})\s*%)");
    std::unordered_set<std::string> seenIds;
    int examTotal=0,otherTotal=0,explicitOtherTotal=0;
    bool hasExam=false,hasOther=false,hasExplicitOther=false;
    for(std::sregex_iterator it(html.begin(),html.end(),element),end;it!=end;++it){
        std::string id=(*it)[2].str();
        std::string normalized=id;
        std::transform(normalized.begin(),normalized.end(),normalized.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        if((normalized.find("pct")==std::string::npos&&
            normalized.find("percent")==std::string::npos)||
           normalized.find("total")!=std::string::npos||
           !seenIds.insert(normalized).second)continue;
        const std::string text=plainText((*it)[3].str());
        std::smatch valueMatch;
        if(!std::regex_search(text,valueMatch,percent))continue;
        const int value=std::stoi(valueMatch[1]);
        if(value>100)continue;

        std::string label=normalized;
        const std::string prefix="div_assessment_";
        if(label.find(prefix)==0)label.erase(0,prefix.size());
        for(const auto*suffix:{"_pct","_percent"}){
            const size_t pos=label.rfind(suffix);
            if(pos!=std::string::npos)label.erase(pos);
        }
        std::replace(label.begin(),label.end(),'_',' ');
        assessment.raw_items.push_back(label+": "+std::to_string(value)+"%");

        if(normalized.find("exam")!=std::string::npos){
            examTotal+=value;
            hasExam=true;
        }else if(normalized.find("_other_pct")!=std::string::npos||
                 normalized.find("_others_pct")!=std::string::npos||
                 normalized.find("_other_percent")!=std::string::npos||
                 normalized.find("_others_percent")!=std::string::npos){
            explicitOtherTotal+=value;
            hasExplicitOther=true;
            hasOther=true;
        }else{
            otherTotal+=value;
            hasOther=true;
        }
    }

    if(hasExplicitOther)otherTotal=explicitOtherTotal;
    if(hasExam&&examTotal<=100)assessment.exam_percent=examTotal;
    if(hasOther&&otherTotal<=100)assessment.other_percent=otherTotal;
    if(assessment.exam_percent&&*assessment.exam_percent==100&&!assessment.other_percent)
        assessment.other_percent=0;
    else if(assessment.other_percent&&*assessment.other_percent==100&&!assessment.exam_percent)
        assessment.exam_percent=0;
    assessment.unknown=!(assessment.exam_percent&&assessment.other_percent&&
        *assessment.exam_percent+*assessment.other_percent==100);
    if(examTotal>100||otherTotal>100)assessment.unknown=true;
    std::string lowerHtml=html;
    std::transform(lowerHtml.begin(),lowerHtml.end(),lowerHtml.begin(),
        [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
    if(lowerHtml.find("indicative only")!=std::string::npos){
        assessment.unknown=true;
        assessment.raw_text="Catalogue assessment is indicative only; verify detailed course information.";
    }
    return assessment;
}

std::vector<std::string> parseCourseOfferingTerms(const std::string &html){
    std::regex element(
        R"(<([a-zA-Z][a-zA-Z0-9]*)\b[^>]*\bid\s*=\s*["'][^"']*course_offering_term[^"']*["'][^>]*>([\s\S]*?)</\1\s*>)",
        std::regex_constants::icase);
    std::smatch match;
    std::string text;
    if(std::regex_search(html,match,element))
        text=plainText(match[2].str());
    else
        text=tableFieldText(html,"Course Offering Term");
    if(text.empty())return {};
    std::transform(text.begin(),text.end(),text.begin(),
        [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
    if(text.find("not offering")!=std::string::npos||
       text.find("not offered")!=std::string::npos)return {"Not offered"};
    std::vector<std::string> terms;
    if(text.find("semester a")!=std::string::npos)terms.push_back("Semester A");
    if(text.find("semester b")!=std::string::npos)terms.push_back("Semester B");
    if(text.find("summer")!=std::string::npos)terms.push_back("Summer");
    return terms;
}

Course parseCoursePageHtml(const std::string &code,const std::string &url,
                           const std::string &html){
    Course course;
   
    course.code=code;
    course.url=url;
    course.catalogue_detail_available=true;
    std::smatch yearMatch;
    if(std::regex_search(html,yearMatch,
        std::regex(R"(Undergraduate Catalogue\s+([0-9]{4}/[0-9]{2}))",
                   std::regex_constants::icase)))
        course.catalogue_year=yearMatch[1].str();
    course.title=elementText(html,"div_course_code_and_title");
    if(course.title.empty())course.title=courseTitleFromTable(html,code);
    if(course.title.rfind(code,0)==0){
        course.title.erase(0,code.size());
        while(!course.title.empty()&&
              (std::isspace(static_cast<unsigned char>(course.title.front()))||
               course.title.front()=='-'||course.title.front()==':'))
            course.title.erase(course.title.begin());
    }

    std::smatch match;
    std::string creditText=elementText(html,"div_course_credits");
    if(creditText.empty())creditText=tableFieldText(html,"Credit Units");
    if(std::regex_search(creditText,match,std::regex(R"(([0-9]+))"))){
        course.credits=std::stoi(match[1]);
        course.credits_known=true;
    }
    course.semesters=parseCourseOfferingTerms(html);
    course.assessment=parseCourseAssessment(html);
    course.exclusive_courses=parseExclusiveCourseCodes(html,code);

    std::string prerequisites=elementText(html,"div_prerequisites");
    if(prerequisites.empty())prerequisites=tableFieldText(html,"Pre-requisite");
    if(prerequisites.empty())prerequisites=tableFieldText(html,"Prerequisites");
    course.prerequisites_text=prerequisites;
    if(!prerequisites.empty()){
        std::string normalized=prerequisites;
        std::transform(normalized.begin(),normalized.end(),normalized.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        if(normalized=="none"||normalized=="nil"||normalized=="not applicable"||
           normalized=="n/a"||normalized=="no prerequisites"){
            course.prerequisite_source="DYNAMIC";
        }else{
            const std::regex codePattern(R"(\b[A-Z]{2,5}\s?[0-9]{4}[A-Z]?\b)");
            for(std::sregex_iterator it(prerequisites.begin(),prerequisites.end(),codePattern),end;
                it!=end;++it){
                std::string prerequisite=it->str();
                prerequisite.erase(std::remove_if(prerequisite.begin(),prerequisite.end(),
                    [](unsigned char ch){return std::isspace(ch);}),prerequisite.end());
                if(std::find(course.prerequisites.begin(),course.prerequisites.end(),prerequisite)==
                   course.prerequisites.end())course.prerequisites.push_back(prerequisite);
            }
            if(course.prerequisites.empty()){
                course.prerequisite_source="UNKNOWN";
                course.prerequisites={"UNKNOWN"};
            }else{
                course.prerequisite_source="DYNAMIC";
            }
        }
    }else{
        course.prerequisite_source="UNKNOWN";
        course.prerequisites={"UNKNOWN"};
    }
    course.area=tableFieldText(html,"GE Area");
    const std::string area=elementText(html,"div_ge_area");
    if(area.find("Area 1")!=std::string::npos)course.area="1";
    else if(area.find("Area 2")!=std::string::npos)course.area="2";
    else if(area.find("Area 3")!=std::string::npos)course.area="3";
    return course;
}

bool Acquisition::loadStaticDb(const std::string &path,std::string&error){
    static_db_path_=path;
    db_.clear();
    if(!loadCoursesDb(path,db_,error))return false;
    const std::string cooldownPath=".cityu_cache/catalogue-cooldown-until";
    std::ifstream cooldown(cooldownPath);
    long long blockedUntil=0;
    if(cooldown>>blockedUntil){
        if(std::time(nullptr)<blockedUntil){
            site_blocked_=true;
            blocked_until_epoch_=blockedUntil;
        }
        else std::filesystem::remove(cooldownPath);
    }
    std::ifstream ov("overrides.json");
    std::string line;
    while(std::getline(ov,line)){
        size_t cp=line.find("\"course\"");
        size_t up=line.find("\"url\"");
        if(cp==std::string::npos||up==std::string::npos)continue;
        size_t cq=line.find('"',cp+10);
        if(cq==std::string::npos)continue;
        size_t ce=line.find('"',cq+1);
        if(ce==std::string::npos)continue;
        std::string code=line.substr(cq+1,ce-cq-1);
        size_t uq=line.find('"',up+6);
        if(uq==std::string::npos)continue;
        size_t ue=line.find('"',uq+1);
        if(ue==std::string::npos)continue;
        overrides_[code]=line.substr(uq+1,ue-uq-1);
    }
    return true;
}

bool Acquisition::loadFallbackDb(const std::string&path,std::string&error){
    fallback_db_.clear();
    if(!loadCoursesDb(path,fallback_db_,error))return false;
    return true;
}

std::vector<std::string> Acquisition::pendingNetworkFetchCodes(
        const std::vector<std::string>&codes,bool refresh) const{
    std::unordered_set<std::string> unique(codes.begin(),codes.end());
    std::vector<std::string> pending;
    for(const auto&code:unique){
        auto local=db_.find(code);
        if(mode_==AcquisitionMode::STATIC_FIRST&&!refresh&&
           local!=db_.end()&&staticCourseComplete(local->second))continue;
        const auto fallback=fallback_db_.find(code);
        if(mode_==AcquisitionMode::STATIC_FIRST&&!refresh&&
           fallback!=fallback_db_.end()&&
           staticCourseComplete(mergeCourseData(
               local==db_.end()?Course{}:local->second,fallback->second)))
            continue;
        if(!refresh&&cacheIsFresh(".cityu_cache/"+code+".html",config_.cache_ttl_hours))continue;
        pending.push_back(code);
    }
    std::sort(pending.begin(),pending.end());
    return pending;
}

std::size_t Acquisition::pendingNetworkFetches(const std::vector<std::string> &codes,
                                               bool refresh) const{
    return pendingNetworkFetchCodes(codes,refresh).size();
}

std::vector<std::string> Acquisition::unavailableWithoutNetwork(
        const std::vector<std::string> &codes,bool refresh) const{
    std::unordered_set<std::string> unique(codes.begin(),codes.end());
    std::vector<std::string> missing;
    for(const auto&code:unique){
        auto local=db_.find(code);
        if(local!=db_.end()&&staticCourseComplete(local->second))continue;
        const auto fallback=fallback_db_.find(code);
        if(fallback!=fallback_db_.end()&&
           staticCourseComplete(mergeCourseData(
               local==db_.end()?Course{}:local->second,fallback->second)))
            continue;
        if(!refresh&&cacheIsFresh(".cityu_cache/"+code+".html",config_.cache_ttl_hours))continue;
        missing.push_back(code);
    }
    std::sort(missing.begin(),missing.end());
    return missing;
}

long long Acquisition::cooldownRemainingSeconds() const{
    return std::max(0LL,blocked_until_epoch_-static_cast<long long>(std::time(nullptr)));
}

void Acquisition::rememberBlock(){
    site_blocked_=true;
    std::filesystem::create_directories(".cityu_cache");
    const long long until=static_cast<long long>(std::time(nullptr))+
        static_cast<long long>(std::max(0,config_.block_cooldown_hours))*60*60;
    blocked_until_epoch_=until;
    std::ofstream cooldown(".cityu_cache/catalogue-cooldown-until",std::ios::trunc);
    cooldown<<until<<"\n";
}

Course Acquisition::dynamicFetch(const std::string &code,const std::string &overrideUrl,bool refresh,bool &ok,std::string &error){
    Course c;c.code=code;c.url="https://www.cityu.edu.hk/catalogue/ug/current/course/"+code+".htm";
    if(!overrideUrl.empty())c.url=overrideUrl;
    std::filesystem::create_directories(".cityu_cache");
    std::string cache=".cityu_cache/"+code+".html";
    std::ifstream cf(cache,std::ios::binary);std::string body;
    if(cf&&!refresh&&cacheIsFresh(cache,config_.cache_ttl_hours))
        body.assign((std::istreambuf_iterator<char>(cf)),std::istreambuf_iterator<char>());
    if(!body.empty()){
        if(challenge(body)){
            error="The catalogue returned a block or challenge page.";
            rememberBlock();
            ok=false;
            return c;
        }
    }
    if(body.empty()&&site_blocked_){
        error="Catalogue access is blocked for this run; no new request was made.";
        ok=false;
        return c;
    }
    if(body.empty()){
        CURL *curl=curl_easy_init();if(!curl){error="Could not start the catalogue connection.";ok=false;return c;}
        long status=0;int retries=config_.max_retries;
        for(int attempt=0;attempt<retries;++attempt){
            if(has_requested_){
                const auto elapsed=std::chrono::steady_clock::now()-last_request_;
                const auto jitterMax=std::max(0,config_.jitter_ms);
                std::uniform_int_distribution<int> jitter(0,jitterMax);
                std::random_device random;
                const int delay=config_.min_request_interval_ms+jitter(random);
                const auto required=std::chrono::milliseconds(std::max(0,delay));
                if(elapsed<required)std::this_thread::sleep_for(required-elapsed);
            }
            last_request_=std::chrono::steady_clock::now();
            has_requested_=true;
            curl_easy_setopt(curl,CURLOPT_URL,c.url.c_str());
            curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
            curl_easy_setopt(curl,CURLOPT_TIMEOUT,30L);
            curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writeCb);
            curl_easy_setopt(curl,CURLOPT_WRITEDATA,&body);
            curl_easy_setopt(curl,CURLOPT_USERAGENT,"CityUStudyPlan/1.0 (+educational planning; respectful request)");
            curl_easy_setopt(curl,CURLOPT_ACCEPT_ENCODING,"");
            body.clear();CURLcode rc=curl_easy_perform(curl);curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
            if(rc==CURLE_OK&&status==200&&!challenge(body)){
                std::ofstream of(cache,std::ios::binary);of<<body;break;
            }
            if(status==403||status==429||challenge(body)){
                error="The catalogue returned a block or challenge response. Further catalogue requests have been stopped for this run.";
                rememberBlock();
                curl_easy_cleanup(curl);
                ok=false;
                return c;
            }
            if(attempt+1<retries){
                long wait=(1000L<<attempt);
                std::this_thread::sleep_for(std::chrono::milliseconds(wait));
            }
        }
        curl_easy_cleanup(curl);
        if(status!=200){error="The catalogue did not return the course page.";ok=false;return c;}
    }
    if(body.find(code)==std::string::npos){error="The page loaded but this course code was not found.";ok=false;return c;}
    c=parseCoursePageHtml(code,c.url,body);
    if(c.title.empty()){
        error="The catalogue page did not contain a readable course title.";
        ok=false;
        return c;
    }
    if(!c.credits_known){
        error="The catalogue page did not contain readable credit units.";
        ok=false;
        return c;
    }
    ok=true;return c;
}

AcquisitionResult Acquisition::fetch(const std::string &code,bool refresh){
    AcquisitionResult r;
    auto it=db_.find(code);
    if(mode_==AcquisitionMode::STATIC_FIRST&&!refresh&&it!=db_.end()&&
       staticCourseComplete(it->second)){
        const auto fallback=fallback_db_.find(code);
        r.course=fallback==fallback_db_.end()
            ?it->second:mergeCourseData(it->second,fallback->second);
        r.course.code=code;return r;
    }
    const auto fallback=fallback_db_.find(code);
    if(mode_==AcquisitionMode::STATIC_FIRST&&!refresh&&
       fallback!=fallback_db_.end()){
        Course effective=it==db_.end()?Course{}:it->second;
        effective=mergeCourseData(effective,fallback->second);
        if(staticCourseComplete(effective)){
            r.course=std::move(effective);
            r.course.code=code;
            return r;
        }
    }
    bool ok=false;std::string error;
    auto ov=overrides_.find(code);
    std::string overrideUrl=(ov==overrides_.end()?"" : ov->second);
    Course c=dynamicFetch(code,overrideUrl,refresh,ok,error);
    if(ok){
        if(it!=db_.end())c=mergeCourseData(c,it->second);
        if(fallback!=fallback_db_.end())
            c=mergeCourseData(c,fallback->second);
        db_[code]=c;
        std::string saveError;
        if(!saveCoursesDb(static_db_path_,db_,saveError))r.database_save_error=saveError;
        r.course=c;
        return r;
    }
    r.needs_fix=true;r.site_blocked=site_blocked_;
    r.error_code=site_blocked_?"FETCH-BLOCKED":"FETCH-FAILED";r.plain_error=error;
    if(it!=db_.end()){
        r.course=fallback==fallback_db_.end()
            ?it->second:mergeCourseData(it->second,fallback->second);
    }else if(fallback!=fallback_db_.end()){
        r.course=fallback->second;
    }else{
        r.course.code=code;
        r.course.url="https://www.cityu.edu.hk/catalogue/ug/current/course/"+code+".htm";
        r.course.assessment.unknown=true;
    }
    return r;
}

