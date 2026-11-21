#include "database_builder.h"
#include "acquisition.h"
#include "json.h"
#include "ui.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace {
std::string trimText(std::string value){
    const size_t first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    const size_t last=value.find_last_not_of(" \t\r\n");
    return value.substr(first,last-first+1);
}
std::string lowerText(std::string value){
    std::transform(value.begin(),value.end(),value.begin(),
        [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
    return value;
}
bool parseCodeTitle(const std::string&line,std::string&code,std::string&title){
    static const std::regex pattern(
        R"(^\s*([A-Z]{2,5}[0-9]{4}[A-Z]?)\s*(?:-\s*|–\s*|—\s*|\s+)(.+?)\s*$)",
        std::regex_constants::icase);
    std::smatch match;
    if(!std::regex_match(line,match,pattern))return false;
    code=match[1].str();
    title=trimText(match[2].str());
    std::transform(code.begin(),code.end(),code.begin(),
        [](unsigned char ch){return static_cast<char>(std::toupper(ch));});
    return !title.empty();
}
bool hasUsableDetail(const Course&course){
    return course.catalogue_detail_available&&!course.title.empty()&&
           course.credits_known&&!course.semesters.empty();
}
std::string officialUrl(const std::string&code){
    return "https://www.cityu.edu.hk/catalogue/ug/current/course/"+code+".htm";
}
std::vector<std::string> linesOf(const std::string&text){
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while(std::getline(input,line))lines.push_back(trimText(line));
    return lines;
}
std::string nextValue(const std::vector<std::string>&lines,size_t index){
    for(size_t i=index+1;i<lines.size();++i)
        if(!lines[i].empty())return lines[i];
    return {};
}
bool isFieldHeading(const std::string&line){
    const std::string lower=lowerText(trimText(line));
    return lower=="offering academic unit"||lower=="credit units"||
        lower=="course duration"||lower.rfind("pre-requisite",0)==0||
        lower.rfind("prerequisite",0)==0||lower.rfind("equivalent course",0)==0||
        lower.rfind("course offering term",0)==0||lower.rfind("exclusive courses",0)==0||
        lower=="course aims"||
        lower.rfind("assessment",0)==0||lower=="detailed course information";
}
void parsePrerequisites(const std::vector<std::string>&lines,size_t index,Course&course){
    std::string text;
    const size_t colon=lines[index].find(':');
    if(colon!=std::string::npos)text=trimText(lines[index].substr(colon+1));
    for(size_t i=index+1;i<lines.size();++i){
        if(lines[i].empty())break;
        if(isFieldHeading(lines[i]))break;
        if(!text.empty())text+=" ";
        text+=lines[i];
    }
    course.prerequisites_text=trimText(text);
    const std::string normalized=lowerText(course.prerequisites_text);
    if(normalized.empty()){
        course.prerequisites={"UNKNOWN"};
        course.prerequisite_source="UNKNOWN";
        return;
    }
    if(normalized=="none"||normalized=="nil"||normalized=="n/a"||
       normalized=="not applicable"||normalized=="no prerequisites"){
        course.prerequisites.clear();
        course.prerequisite_source="STATIC";
        return;
    }
    static const std::regex codePattern(R"(\b[A-Z]{2,5}\s*[0-9]{4}[A-Z]?\b)",
                                       std::regex_constants::icase);
    std::vector<std::string> codes;
    for(std::sregex_iterator it(course.prerequisites_text.begin(),
                                course.prerequisites_text.end(),codePattern),end;
        it!=end;++it){
        std::string code=it->str();
        code.erase(std::remove_if(code.begin(),code.end(),
            [](unsigned char ch){return std::isspace(ch);}),code.end());
        std::transform(code.begin(),code.end(),code.begin(),
            [](unsigned char ch){return static_cast<char>(std::toupper(ch));});
        if(std::find(codes.begin(),codes.end(),code)==codes.end())codes.push_back(code);
    }
    const bool ambiguous=normalized.find(" or ")!=std::string::npos||
        normalized.find('/')!=std::string::npos||normalized.find("either")!=std::string::npos||
        normalized.find("minimum")!=std::string::npos||normalized.find("eligible")!=std::string::npos;
    if(codes.empty()||ambiguous){
        course.prerequisites={"UNKNOWN"};
        course.prerequisite_source="UNKNOWN";
    }else{
        course.prerequisites=std::move(codes);
        course.prerequisite_source="STATIC";
    }
}
void parseExclusiveCourses(const std::vector<std::string>&lines,size_t index,
                           Course&course){
    std::string text;
    const size_t colon=lines[index].find(':');
    if(colon!=std::string::npos)text=trimText(lines[index].substr(colon+1));
    for(size_t i=index+1;i<lines.size();++i){
        if(lines[i].empty())break;
        if(isFieldHeading(lines[i]))break;
        if(!text.empty())text+=" ";
        text+=lines[i];
    }
    static const std::regex codePattern(R"(\b[A-Z]{2,5}\s*[0-9]{4}[A-Z]?\b)",
                                       std::regex_constants::icase);
    for(std::sregex_iterator it(text.begin(),text.end(),codePattern),end;it!=end;++it){
        std::string code=it->str();
        code.erase(std::remove_if(code.begin(),code.end(),
            [](unsigned char ch){return std::isspace(ch);}),code.end());
        std::transform(code.begin(),code.end(),code.begin(),
            [](unsigned char ch){return static_cast<char>(std::toupper(ch));});
        if(code!=course.code&&
           std::find(course.exclusive_courses.begin(),course.exclusive_courses.end(),code)==
               course.exclusive_courses.end())
            course.exclusive_courses.push_back(code);
    }
}
void parseAssessment(const std::vector<std::string>&lines,size_t index,Course&course){
    const std::string heading=lowerText(lines[index]);
    const bool indicative=heading.find("indicative only")!=std::string::npos;
    std::regex component(R"(^\s*([^:]+):\s*([0-9]{1,3})\s*%\s*$)",
                         std::regex_constants::icase);
    std::regex percent(R"(([0-9]{1,3})\s*%)");
    int exam=0,other=0;
    bool hasExam=false,hasOther=false;
    for(size_t i=index+1;i<lines.size();++i){
        if(lowerText(lines[i]).find("detailed course information")!=std::string::npos)break;
        std::smatch match;
        if(!std::regex_match(lines[i],match,component))continue;
        const std::string label=trimText(match[1].str());
        const std::string lowerLabel=lowerText(label);
        if(lowerLabel.find("min.")!=std::string::npos||
           lowerLabel.find("passing")!=std::string::npos||
           lowerLabel.find("duration")!=std::string::npos)continue;
        const int value=std::stoi(match[2]);
        if(value>100)continue;
        course.assessment.raw_items.push_back(label+": "+std::to_string(value)+"%");
        if(lowerLabel.find("exam")!=std::string::npos){
            exam+=value;
            hasExam=true;
        }else{
            other+=value;
            hasOther=true;
        }
    }
    if(hasExam&&exam<=100)course.assessment.exam_percent=exam;
    if(hasOther&&other<=100)course.assessment.other_percent=other;
    if(hasExam&&!hasOther&&exam==100)course.assessment.other_percent=0;
    else if(hasOther&&!hasExam&&other==100)course.assessment.exam_percent=0;
    const bool complete=course.assessment.exam_percent&&course.assessment.other_percent&&
        *course.assessment.exam_percent+*course.assessment.other_percent==100;
    course.assessment.unknown=indicative||!complete||exam>100||other>100;
    course.assessment.raw_text=indicative
        ?"Catalogue assessment is indicative only; verify detailed course information."
        :"";
}
Course mergeCourse(const Course&old,const Course&incoming){
    Course merged=old;
    merged.code=incoming.code;
    if(!incoming.title.empty())merged.title=incoming.title;
    if(incoming.credits_known){merged.credits=incoming.credits;merged.credits_known=true;}
    if(!incoming.semesters.empty())merged.semesters=incoming.semesters;
    const bool incomingIsNewer=!incoming.catalogue_year.empty()&&
        incoming.catalogue_year>=old.catalogue_year;
    if(incoming.prerequisite_source!="UNKNOWN"||old.prerequisite_source=="UNKNOWN"||
       incomingIsNewer){
        merged.prerequisites=incoming.prerequisites;
        merged.prerequisites_text=incoming.prerequisites_text;
        merged.prerequisite_source=incoming.prerequisite_source;
    }else if(merged.prerequisites_text.empty()){
        merged.prerequisites_text=old.prerequisites_text;
    }
    if(!incoming.catalogue_year.empty())merged.catalogue_year=incoming.catalogue_year;
    merged.catalogue_detail_available=old.catalogue_detail_available||
                                      incoming.catalogue_detail_available;
    if(!incoming.area.empty())merged.area=incoming.area;
    if(!incoming.tags.empty())merged.tags=incoming.tags;
    if(!incoming.exclusive_courses.empty())
        merged.exclusive_courses=incoming.exclusive_courses;
    if(!incoming.catalogue_notes.empty())
        merged.catalogue_notes=incoming.catalogue_notes;
    if(!incoming.url.empty())merged.url=incoming.url;
    if(!incoming.assessment.raw_items.empty()||!incoming.assessment.raw_text.empty()){
        if(old.assessment.unknown||incoming.assessment.unknown){
            if(old.assessment.unknown||!incoming.assessment.unknown)
                merged.assessment=incoming.assessment;
            else{
                merged.assessment.raw_items=incoming.assessment.raw_items;
                merged.assessment.raw_text=incoming.assessment.raw_text;
            }
        }
    }
    return merged;
}
std::string durationText(long long seconds){
    if(seconds<60)return std::to_string(seconds)+" seconds";
    if(seconds<3600)return std::to_string(seconds/60)+" minutes";
    if(seconds<86400)return std::to_string(seconds/3600)+" hours "+
        std::to_string((seconds%3600)/60)+" minutes";
    return std::to_string(seconds/86400)+" days "+
        std::to_string((seconds%86400)/3600)+" hours";
}
}

bool parsePastedCourseDatabase(const std::string&text,
                               PastedCourseDatabase&database,
                               std::string&error){
    database={};
    std::smatch yearMatch;
    const std::regex yearPattern(R"(Undergraduate Catalogue\s+([0-9]{4}/[0-9]{2})?)",
                                 std::regex_constants::icase);
    if(std::regex_search(text,yearMatch,yearPattern))database.catalogue_year=yearMatch[1].str();
    constexpr const char*marker="COURSES >>>";
    const size_t firstDetail=text.find(marker);
    const auto allLines=linesOf(text.substr(
        0,firstDetail==std::string::npos?std::string::npos:firstDetail));
    std::string code,title;
    for(const auto&line:allLines){
        if(!parseCodeTitle(line,code,title))continue;
        Course&course=database.courses[code];
        course.code=code;
        course.title=title;
        course.prerequisite_source="UNKNOWN";
        course.prerequisites={"UNKNOWN"};
        course.url=officialUrl(code);
    }

    size_t position=0;
    while((position=text.find(marker,position))!=std::string::npos){
        ++database.detail_blocks_seen;
        const size_t bodyStart=position+std::char_traits<char>::length(marker);
        const size_t next=text.find(marker,bodyStart);
        const auto blockLines=linesOf(text.substr(bodyStart,
            next==std::string::npos?std::string::npos:next-bodyStart));
        size_t headingIndex=blockLines.size();
        Course detail;
        for(size_t i=0;i<std::min<size_t>(blockLines.size(),40);++i){
            if(parseCodeTitle(blockLines[i],code,title)){
                headingIndex=i;
                detail.code=code;
                detail.title=title;
                detail.url=officialUrl(code);
                detail.catalogue_year=database.catalogue_year;
                detail.catalogue_detail_available=true;
                detail.prerequisites={"UNKNOWN"};
                break;
            }
        }
        if(headingIndex==blockLines.size()){
            ++database.unparsed_detail_blocks;
            position=bodyStart;
            continue;
        }
        for(size_t i=headingIndex+1;i<blockLines.size();++i){
            const std::string label=lowerText(blockLines[i]);
            if(label=="credit units"){
                const std::string value=nextValue(blockLines,i);
                std::smatch creditMatch;
                if(std::regex_search(value,creditMatch,std::regex(R"(([0-9]+))"))){
                    detail.credits=std::stoi(creditMatch[1]);
                    detail.credits_known=true;
                }
            }else if(label.rfind("course offering term",0)==0){
                const std::string value=lowerText(nextValue(blockLines,i));
                if(value.find("not offering")!=std::string::npos){
                    detail.semesters={"Not offered"};
                }else{
                    if(value.find("semester a")!=std::string::npos)detail.semesters.push_back("Semester A");
                    if(value.find("semester b")!=std::string::npos)detail.semesters.push_back("Semester B");
                    if(value.find("summer")!=std::string::npos)detail.semesters.push_back("Summer");
                }
            }else if(label.rfind("pre-requisite",0)==0||label.rfind("prerequisite",0)==0){
                parsePrerequisites(blockLines,i,detail);
            }else if(label.rfind("exclusive courses",0)==0){
                parseExclusiveCourses(blockLines,i,detail);
            }else if(label.rfind("assessment",0)==0){
                parseAssessment(blockLines,i,detail);
                break;
            }
        }
        if(detail.prerequisite_source.empty()){
            detail.prerequisite_source="UNKNOWN";
            detail.prerequisites={"UNKNOWN"};
        }
        Course&stored=database.courses[detail.code];
        if(stored.code.empty())stored=detail;
        else stored=mergeCourse(stored,detail);
        database.detailed_codes.insert(detail.code);
        position=bodyStart;
    }
    if(database.courses.empty()){
        error="No course codes were recognized. Check that the file contains the course index or course pages.";
        return false;
    }
    return true;
}

int runDatabaseBuilder(const std::string&sourcePath,bool importOnly,bool fetchOnly,
                       Config config){
    PastedCourseDatabase parsed;
    std::unordered_map<std::string,Course> database;
    std::string loadError;
    if(fetchOnly){
        if(!std::filesystem::exists("courses_db.json")||
           !loadCoursesDb("courses_db.json",database,loadError)){
            printWarning("Could not read courses_db.json. Import course text first with --database <file> --import-only.");
            return 2;
        }
        printSection("Fill missing catalogue details");
        printInfo("Using courses_db.json only; no source text will be imported.");
    }else{
        std::ifstream input(sourcePath,std::ios::binary);
        if(!input){
            printWarning("Could not open "+sourcePath+". Pass the pasted course text file path after --database.");
            return 2;
        }
        const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        std::string parseError;
        if(!parsePastedCourseDatabase(text,parsed,parseError)){
            printWarning(parseError);
            return 2;
        }
        if(std::filesystem::exists("courses_db.json")&&
           !loadCoursesDb("courses_db.json",database,loadError)){
            printWarning("Could not read the existing database: "+loadError);
            return 3;
        }
        for(const auto&[code,incoming]:parsed.courses){
            auto current=database.find(code);
            if(current==database.end())database.emplace(code,incoming);
            else current->second=mergeCourse(current->second,incoming);
        }
        std::string saveError;
        if(!saveCoursesDb("courses_db.json",database,saveError)){
            printWarning("Imported data could not be saved: "+saveError);
            return 3;
        }
        printSection("Local course database");
        printSuccess("Imported "+std::to_string(parsed.courses.size())+
                     " course codes, including "+std::to_string(parsed.detailed_codes.size())+
                     " detailed records, from "+sourcePath+".");
        printInfo("Catalogue version: "+(parsed.catalogue_year.empty()?"not found":parsed.catalogue_year)+
                  "; database now contains "+std::to_string(database.size())+" courses.");
        if(parsed.unparsed_detail_blocks>0)
            printWarning(std::to_string(parsed.unparsed_detail_blocks)+
                         " detail blocks could not be matched to a course heading.");
        if(importOnly)return 0;
    }

    config.max_retries=1;
    Acquisition acquisition(config,AcquisitionMode::STATIC_FIRST);
    std::string acquisitionLoadError;
    if(!acquisition.loadStaticDb("courses_db.json",acquisitionLoadError)){
        printWarning("Could not load the course database for catalogue fill: "+
                     acquisitionLoadError);
        return 2;
    }
    std::vector<std::string> toFill;
    if(fetchOnly){
        toFill.reserve(database.size());
        for(const auto&[code,course]:database)toFill.push_back(code);
    }else{
        for(const auto&[code,course]:parsed.courses){
            auto existing=acquisition.staticDb().find(code);
            if(existing!=acquisition.staticDb().end()&&
               !parsed.catalogue_year.empty()&&
               existing->second.catalogue_year==parsed.catalogue_year&&
               hasUsableDetail(existing->second))continue;
            toFill.push_back(code);
        }
    }
    std::sort(toFill.begin(),toFill.end());
    const std::size_t requestCount=acquisition.pendingNetworkFetches(toFill,false);
    if(requestCount==0){
        printSuccess("No additional catalogue requests are needed from this source list.");
        return 0;
    }
    const long long interval=std::max(0,config.min_request_interval_ms);
    const long long jitter=std::max(0,config.jitter_ms);
    const long long gaps=static_cast<long long>(requestCount-1);
    const long long minimum=gaps*interval/1000;
    const long long maximum=static_cast<long long>(requestCount)*30+
        gaps*(interval+jitter)/1000;
    printInfo(std::to_string(requestCount)+" courses still need page details.");
    printInfo("With one request at a time and 60–90 second gaps, expect about "+
              durationText(minimum)+" to "+durationText(maximum)+
              ". The request ends immediately on a block or challenge.");
    if(acquisition.catalogueCoolingDown()){
        printWarning("The saved catalogue cooldown is active; no network request will be sent.");
        return 0;
    }
    if(!yesNo("Start the conservative catalogue fill now? You can stop this step with Ctrl+C.")){
        printInfo(fetchOnly?"No web request was started.":"Database import is complete; automatic lookup was not started.");
        return 0;
    }
    size_t completed=0,failed=0;
    for(size_t i=0;i<toFill.size();++i){
        const auto&code=toFill[i];
        std::cout<<"\n["<<i+1<<"/"<<toFill.size()<<"] Looking up "<<code<<"\n";
        const auto result=acquisition.fetch(code,false);
        if(!result.database_save_error.empty())
            printWarning("Could not save this update: "+result.database_save_error);
        if(result.needs_fix){
            ++failed;
            printWarning(code+": "+result.plain_error);
            if(result.site_blocked){
                printWarning("The catalogue blocked or challenged access. No more requests will be made; completed records have already been saved.");
            }else{
                printWarning("The lookup failed. Stopping rather than sending more requests; completed records have already been saved.");
            }
            break;
        }else{
            ++completed;
            printSuccess(code+" saved.");
        }
    }
    printSection("Database update result");
    printInfo("Updated "+std::to_string(completed)+" courses; "+
              std::to_string(failed)+" unresolved lookup(s).");
    return failed?1:0;
}
