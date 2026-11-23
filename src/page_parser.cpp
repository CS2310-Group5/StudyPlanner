#include "page_parser.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace {
std::string trim(std::string value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    const auto last=value.find_last_not_of(" \t\r\n");
    return value.substr(first,last-first+1);
}

std::string lower(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),
        [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
    return value;
}

std::string upper(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),
        [](unsigned char ch){return static_cast<char>(std::toupper(ch));});
    return value;
}

std::vector<std::string> linesOf(const std::string &text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while(std::getline(input,line))lines.push_back(trim(line));
    return lines;
}

std::string previousValue(const std::vector<std::string> &lines,
                          const std::string &label) {
    const std::string wanted=lower(label);
    static const std::unordered_set<std::string> labels={
        "name","student id","programme","college","level","degree","major",
        "cohort","department","max credits permitted",
        "expected graduation date","email","advisor","student mentor",
        "worksheets","plans","links"
    };
    const auto validValue=[&](const std::string &candidate) {
        const std::string normalized=lower(candidate);
        if(candidate.empty()||labels.count(normalized)||normalized==wanted)
            return false;
        return !std::regex_match(normalized,std::regex(R"((name\s*){2,})"));
    };
    for(size_t i=0;i<lines.size();++i) {
        const std::string current=lower(lines[i]);
        if(current==wanted) {
            for(size_t j=i;j>0;--j)
                if(!lines[j-1].empty()) {
                    if(validValue(lines[j-1]))return lines[j-1];
                    break;
                }
            for(size_t j=i+1;j<lines.size();++j)
                if(!lines[j].empty()) {
                    if(validValue(lines[j]))return lines[j];
                    break;
                }
        }
        if(current.rfind(wanted+":",0)==0)return trim(lines[i].substr(label.size()+1));
    }
    return {};
}

std::string languagePathForCode(const std::string &code) {
    if(code.rfind("LC24",0)==0)return "French";
    if(code.rfind("LC25",0)==0)return "German";
    if(code.rfind("LC26",0)==0)return "Spanish";
    if(code.rfind("LC293",0)==0)return "Japanese";
    if(code.rfind("LC297",0)==0)return "Korean";
    if(code.rfind("PIA26",0)==0)return "Computational Language";
    return {};
}

std::string languagePathForGroup(const ChoiceGroup &group) {
    std::string path;
    for(const auto &code:group.candidates) {
        const std::string candidate=languagePathForCode(code);
        if(candidate.empty()||(!path.empty()&&candidate!=path))return {};
        path=candidate;
    }
    return path;
}

std::string capture(const std::string &text,const std::regex &pattern,
                    size_t group=1) {
    std::smatch match;
    if(std::regex_search(text,match,pattern)&&match.size()>group)
        return trim(match[group].str());
    return {};
}

std::vector<std::string> courseCodes(const std::string &text) {
    static const std::regex token(R"(\b([A-Z]{2,5})?\s*([0-9]{4}[A-Z]?)\b)",
                                  std::regex_constants::icase);
    std::vector<std::string> result;
    std::string prefix;
    size_t previousEnd=0;
    for(std::sregex_iterator it(text.begin(),text.end(),token),end;it!=end;++it) {
        const std::smatch &match=*it;
        const std::string explicitPrefix=upper(match[1].str());
        const std::string digits=upper(match[2].str());
        if(explicitPrefix=="OR"||explicitPrefix=="AND") {
            if(!prefix.empty())result.push_back(prefix+digits);
        } else if(!explicitPrefix.empty()) {
            prefix=explicitPrefix;
            result.push_back(prefix+digits);
        } else if(!prefix.empty()) {
            const std::string separator=lower(text.substr(previousEnd,
                static_cast<size_t>(match.position())-previousEnd));
            if(separator.find("or")!=std::string::npos||
               separator.find("and")!=std::string::npos||
               separator.find('/')!=std::string::npos||
               separator.find(',')!=std::string::npos||
               separator.find('&')!=std::string::npos)
                result.push_back(prefix+digits);
        }
        previousEnd=static_cast<size_t>(match.position()+match.length());
    }
    std::vector<std::string> unique;
    for(const auto &code:result)
        if(std::find(unique.begin(),unique.end(),code)==unique.end())
            unique.push_back(code);
    return unique;
}

std::string normalizedSemester(const std::string &value) {
    const std::string text=lower(value);
    if(std::regex_search(text,std::regex(R"(\b(summer|summer\s+\d{4})\b)")))
        return "Summer";
    if(std::regex_search(text,std::regex(R"(\b(semester\s*a|sem\s*a)\b)")))
        return "Semester A";
    if(std::regex_search(text,std::regex(R"(\b(semester\s*b|sem\s*b)\b)")))
        return "Semester B";
    return {};
}

bool parseCredits(const std::string &value,int &credits) {
    static const std::regex pattern(R"(^\s*\(?\s*(\d+)\s*\)?\s*(?:credits?)?\s*$)",
                                    std::regex_constants::icase);
    std::smatch match;
    if(!std::regex_match(value,match,pattern))return false;
    try { credits=std::stoi(match[1].str());return true; }
    catch(...) { return false; }
}

bool parseDate(const std::string &value,int &year,int &month,int &day) {
    static const std::regex dmy(R"(^\s*(\d{1,2})[-/ ]([A-Za-z]{3,9}|\d{1,2})[-/ ](\d{4})\s*$)");
    std::smatch match;
    if(!std::regex_match(value,match,dmy))return false;
    day=std::stoi(match[1].str());
    year=std::stoi(match[3].str());
    const std::string monthText=lower(match[2].str());
    static const std::vector<std::string> months={
        "jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec"};
    if(std::isdigit(static_cast<unsigned char>(monthText[0])))month=std::stoi(monthText);
    else {
        const auto found=std::find_if(months.begin(),months.end(),
            [&](const std::string &name){return monthText.rfind(name,0)==0;});
        if(found==months.end())return false;
        month=static_cast<int>(found-months.begin())+1;
    }
    return month>=1&&month<=12&&day>=1&&day<=31;
}

void parseMetadata(const std::string &text,
                   const std::vector<std::string> &lines,
                   DegreeRequirements &req) {
    req.student_name=previousValue(lines,"Name");
    req.student_id=previousValue(lines,"Student ID");
    req.programme=previousValue(lines,"Programme");

    req.college=capture(text,std::regex(
        R"(College\s+(.+?)\s*Level\b)",std::regex_constants::icase));
    req.degree=capture(text,std::regex(
        R"(Degree\s+(.+?)\s*Major\b)",std::regex_constants::icase));
    req.major=capture(text,std::regex(
        R"(Major\s+(.+?)\s*Cohort\b)",std::regex_constants::icase));
    req.cohort=capture(text,std::regex(
        R"(Cohort\s+(\d{4})(?!\d))",std::regex_constants::icase));
    req.department=capture(text,std::regex(
        R"(Department\s+(.+?)\s*Max\s+Credits\s+Permitted\b)",
        std::regex_constants::icase));
    const std::string maxCredits=capture(text,std::regex(
        R"(Max\s+Credits\s+Permitted\s*:?\s*(\d+))",
        std::regex_constants::icase));
    if(!maxCredits.empty()) {
        try { req.max_credits_permitted=std::stoi(maxCredits); }
        catch(...) {}
    }
    const std::string auditCredits=capture(text,std::regex(
        R"(\b(\d+)\s+Credit\s+Units?\s+needed\b)",
        std::regex_constants::icase));
    if(!auditCredits.empty()) {
        try { req.audit_remaining_credits=std::stoi(auditCredits); }
        catch(...) {}
    }
    req.expected_graduation_date=capture(text,std::regex(
        R"(Expected\s+Graduation\s+Date\s*:?\s*(\d{1,2}[-/ ][A-Za-z]{3,9}[-/ ]\d{4}|\d{1,2}/\d{1,2}/\d{4}))",
        std::regex_constants::icase));
    req.audit_date=capture(text,std::regex(
        R"(Audit\s+Date\s*:?\s*(\d{1,2}[-/ ][A-Za-z]{3,9}[-/ ]\d{4}|\d{1,2}/\d{1,2}/\d{4}))",
        std::regex_constants::icase));
    req.entry_status=capture(text,std::regex(
        R"(Advanced\s+Standing\s+(I{1,2}|1|2)\b)",
        std::regex_constants::icase));
    if(!req.entry_status.empty()) {
        req.entry_status=upper(req.entry_status)=="II"||
                         req.entry_status=="2" ?
                         "Advanced Standing II":"Advanced Standing I";
    }

    int startYear=0,startMonth=0,startDay=0,endYear=0,endMonth=0,endDay=0;
    if(parseDate(req.audit_date,startYear,startMonth,startDay)&&
       parseDate(req.expected_graduation_date,endYear,endMonth,endDay)) {
        std::tm start{};
        start.tm_year=startYear-1900;start.tm_mon=startMonth-1;start.tm_mday=startDay;
        std::tm end{};
        end.tm_year=endYear-1900;end.tm_mon=endMonth-1;end.tm_mday=endDay;
        const std::time_t startTime=std::mktime(&start);
        const std::time_t endTime=std::mktime(&end);
        if(startTime!=-1&&endTime>startTime) {
            const double years=std::difftime(endTime,startTime)/(365.2425*24*60*60);
            req.remaining_years=std::clamp(static_cast<int>(std::ceil(years)),1,4);
        }
    }
    if(req.remaining_years==0&&req.entry_status=="Advanced Standing I")
        req.remaining_years=3;
    if(req.remaining_years==0&&req.entry_status=="Advanced Standing II")
        req.remaining_years=2;
}

void addInProgress(DegreeRequirements &req,InProgressCourse course,
                   std::unordered_set<std::string> &seen) {
    course.code=upper(course.code);
    if(course.code.empty()||!seen.insert(course.code).second)return;
    if(!course.title.empty())req.course_titles[course.code]=course.title;
    req.in_progress.push_back(course.code);
    req.in_progress_details.push_back(std::move(course));
}

void parseInProgressTable(const std::vector<std::string> &lines,
                          DegreeRequirements &req) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> terms;
    bool inTable=false;
    for(size_t i=0;i<lines.size();++i) {
        const std::string low=lower(lines[i]);
        if(low=="in-progress"||low=="in progress") {
            inTable=true;
            continue;
        }
        if(!inTable) {
            const auto marker=low.find("in progress:");
            if(marker!=std::string::npos) {
                for(const auto &code:courseCodes(lines[i].substr(marker+12))) {
                    InProgressCourse course;course.code=code;
                    addInProgress(req,std::move(course),seen);
                }
            }
            continue;
        }
        if(low=="legend"||low=="degree audit legend"||low=="end of report"||
           low=="still needed"||low=="still needed:"||
           low=="completed"||low=="completed courses"||
           low=="completed courses:")
            break;
        if(low.find("course")!=std::string::npos&&
           low.find("title")!=std::string::npos)continue;
        if(lines[i].find('\t')!=std::string::npos) {
            std::vector<std::string> fields;
            std::istringstream row(lines[i]);
            std::string field;
            while(std::getline(row,field,'\t'))fields.push_back(trim(field));
            if(fields.size()>=2) {
                const auto codes=courseCodes(fields[0]);
                if(!codes.empty()) {
                    InProgressCourse course;course.code=codes.front();
                    if(fields.size()>1)course.title=fields[1];
                    if(fields.size()>2)course.grade=fields[2];
                    if(fields.size()>3)course.credits_known=
                        parseCredits(fields[3],course.credits);
                    if(fields.size()>4)course.term=normalizedSemester(fields[4]);
                    if(!course.term.empty())terms.push_back(course.term);
                    addInProgress(req,std::move(course),seen);
                }
            }
            continue;
        }
        const auto codes=courseCodes(lines[i]);
        if(codes.empty())continue;
        InProgressCourse course;course.code=codes.front();
        const std::string matched=upper(std::regex_replace(
            lines[i],std::regex(R"(\b[A-Z]{2,5}\s*[0-9]{4}[A-Z]?\b)",
                                std::regex_constants::icase),""));
        course.title=trim(matched);
        for(size_t j=i+1;j<lines.size()&&j<=i+6;++j) {
            const std::string next=lines[j];
            if(next.empty())continue;
            if(!course.title.empty()&&j==i+1)continue;
            if(!course.term.empty())break;
            const std::string term=normalizedSemester(next);
            if(!term.empty()) { course.term=term;terms.push_back(term);break; }
            int credits=0;
            if(!course.credits_known&&parseCredits(next,credits)) {
                course.credits=credits;course.credits_known=true;continue;
            }
            if(course.title.empty()) {course.title=next;continue;}
            if(course.grade.empty())course.grade=next;
        }
        addInProgress(req,std::move(course),seen);
    }
    if(!terms.empty()) {
        std::sort(terms.begin(),terms.end());
        terms.erase(std::unique(terms.begin(),terms.end()),terms.end());
        req.conflicting_in_progress_terms=terms.size()>1;
        if(terms.size()==1)req.current_semester=terms.front();
    }
}

void parseCompletedCourses(const std::vector<std::string> &lines,
                           DegreeRequirements &req) {
    bool inCompletedSection=false;
    std::unordered_set<std::string> seen(req.in_progress.begin(),
                                         req.in_progress.end());
    for(const auto&line:lines){
        const std::string label=lower(line);
        const bool completedHeader=label=="completed"||
            label=="completed:"||label=="completed courses"||
            label=="completed courses:"||label=="courses completed"||
            label=="completed requirements";
        if(completedHeader){
            inCompletedSection=true;
            continue;
        }
        if(label=="in-progress"||label=="in progress"||
           label=="still needed"||label=="legend"||
           label=="degree audit legend"||label=="end of report"||
           label.find("gateway education")!=std::string::npos){
            inCompletedSection=false;
            continue;
        }
        if(!inCompletedSection)continue;
        for(const auto&code:courseCodes(line))
            if(seen.insert(code).second)
                req.completed_courses.push_back(code);
    }
}

ChoiceGroup parseChoice(const std::smatch &match,const std::string &wholeText) {
    ChoiceGroup group;
    const int amount=std::stoi(match[1].str());
    const std::string unit=lower(match[2].str());
    if(unit.find("course")!=std::string::npos||
       unit.find("class")!=std::string::npos)group.choose=amount;
    else group.required_credits=amount;
    const std::string rest=match[3].str();
    group.candidates=courseCodes(rest);
    static const std::regex courseCodePattern(R"(([A-Z]{2,6})\d{3,5}[A-Z]?)",
                                             std::regex_constants::icase);
    std::smatch courseMatch;
    if(std::regex_search(rest,courseMatch,courseCodePattern)) {
        const size_t codePosition=static_cast<size_t>(courseMatch.position());
        group.purpose=trim(rest.substr(0,codePosition));
    } else {
        group.purpose=trim(rest);
    }
    static const std::regex areaPattern(R"(\b([1-9])\b)");
    const auto areaStart=lower(rest).find("area");
    if(areaStart!=std::string::npos) {
        const std::string areaList=rest.substr(areaStart+4);
        for(std::sregex_iterator it(areaList.begin(),areaList.end(),areaPattern),
            end;it!=end;++it) {
            const int area=std::stoi((*it)[1].str());
            if(std::find(group.areas.begin(),group.areas.end(),area)==group.areas.end())
                group.areas.push_back(area);
        }
    }
    if(!group.areas.empty()) {
        static const std::regex minimumPattern(
            R"(Area\s*([1-9])\s*[:\-]?\s*Minimum\s*(\d+)\s*Credit)",
            std::regex_constants::icase);
        for(std::sregex_iterator it(wholeText.begin(),wholeText.end(),minimumPattern),
            end;it!=end;++it) {
            const int area=std::stoi((*it)[1].str());
            const int minimum=std::stoi((*it)[2].str());
            if(std::find(group.areas.begin(),group.areas.end(),area)!=group.areas.end()&&
               (group.minimum_per_area_credits==0||
                minimum<group.minimum_per_area_credits))
                group.minimum_per_area_credits=minimum;
        }
    }
    return group;
}

std::vector<ChoiceGroup> parseChoiceGroups(const std::string &text) {
    static const std::regex choicePattern(
        R"((\d+)\s*(credits?|credit\s+units?|courses?|classes?)\s*(?:in|of|from)\s+(.+))",
        std::regex_constants::icase);
    static const std::regex eitherPattern(
        R"(Either\s+(.+?)\s+can\s+be\s+chosen\s+to\s+fulfill\s+(.+))",
        std::regex_constants::icase);
    const auto lines=linesOf(text);
    const bool hasAuditStatuses=std::regex_search(text,
        std::regex(R"(\b(Not\s+complete|Still\s+needed)\b)",
                   std::regex_constants::icase));
    std::vector<ChoiceGroup> groups;
    bool followingStillNeeded=false;
    bool previousIncomplete=false;
    for(size_t i=0;i<lines.size();++i) {
        const std::string low=lower(lines[i]);
        if(low.find("still needed")!=std::string::npos)
            followingStillNeeded=true;
        const bool incompleteLine=low.find("not complete")!=std::string::npos||
                                  low.find("still needed")!=std::string::npos;
        const bool eligibleLine=!hasAuditStatuses||incompleteLine||
                                previousIncomplete||
                                followingStillNeeded;
        if(eligibleLine) {
            for(std::sregex_iterator it(lines[i].begin(),lines[i].end(),
                choicePattern),end;it!=end;++it) {
                ChoiceGroup group=parseChoice(*it,text);
                if(group.choose==0&&group.required_credits==0)continue;
                if(group.candidates.empty()&&group.areas.empty())continue;
                const auto same=std::find_if(groups.begin(),groups.end(),
                    [&](const ChoiceGroup &existing) {
                        return existing.choose==group.choose&&
                               existing.required_credits==group.required_credits&&
                               existing.candidates==group.candidates&&
                               existing.areas==group.areas;
                    });
                if(same==groups.end())groups.push_back(std::move(group));
            }
            std::smatch either;
            if(std::regex_search(lines[i],either,eitherPattern)) {
                ChoiceGroup group;
                group.choose=1;
                group.candidates=courseCodes(either[1].str());
                group.purpose=trim(either[2].str());
                if(!group.candidates.empty())
                    groups.push_back(std::move(group));
            }
        }
        if(low=="in-progress"||low=="in progress"||
           low=="legend"||low=="degree audit legend"||
           low=="end of report"||
           (low.find("met requirement")!=std::string::npos)||
           (low.find("complete")!=std::string::npos&&
            low.find("not complete")==std::string::npos))
            followingStillNeeded=false;
        previousIncomplete=incompleteLine;
    }
    return groups;
}
}

DegreeRequirements parseDegreeWorks(const std::string &text) {
    DegreeRequirements req;
    const auto lines=linesOf(text);
    parseMetadata(text,lines,req);
    parseInProgressTable(lines,req);
    parseCompletedCourses(lines,req);

    req.choice_groups=parseChoiceGroups(text);

    std::unordered_set<std::string> activeLanguagePaths;
    for(const auto &code:req.in_progress) {
        const std::string path=languagePathForCode(code);
        if(!path.empty())activeLanguagePaths.insert(path);
    }
    if(activeLanguagePaths.size()==1) {
        const std::string activePath=*activeLanguagePaths.begin();
        req.choice_groups.erase(std::remove_if(req.choice_groups.begin(),
            req.choice_groups.end(),[&](ChoiceGroup &group) {
                const std::string path=languagePathForGroup(group);
                if(path.empty())return false;
                if(path!=activePath)return true;
                int completedCredits=0;
                for(const auto&code:group.candidates)
                    if(std::find(req.in_progress.begin(),req.in_progress.end(),
                                 code)!=req.in_progress.end()){
                        const auto detail=std::find_if(
                            req.in_progress_details.begin(),
                            req.in_progress_details.end(),
                            [&code](const InProgressCourse&course){
                                return course.code==code;
                            });
                        completedCredits+=detail!=req.in_progress_details.end()&&
                            detail->credits_known?detail->credits:3;
                    }
                group.candidates.erase(std::remove_if(group.candidates.begin(),
                    group.candidates.end(),[&](const std::string &code) {
                        return std::find(req.in_progress.begin(),
                                         req.in_progress.end(),code)!=
                               req.in_progress.end();
                    }),group.candidates.end());
                group.required_credits=std::max(
                    0,group.required_credits-completedCredits);
                return group.required_credits==0||group.candidates.empty();
            }),req.choice_groups.end());
    } else if(activeLanguagePaths.empty()) {
        std::vector<ChoiceGroup> languageOptions;
        std::vector<ChoiceGroup> retained;
        for(auto &group:req.choice_groups) {
            if(group.required_credits==6&&!languagePathForGroup(group).empty())
                languageOptions.push_back(std::move(group));
            else
                retained.push_back(std::move(group));
        }
        if(languageOptions.size()>1) {
            ChoiceGroup combined;
            combined.required_credits=6;
            combined.purpose="one language or computational language option";
            for(const auto &option:languageOptions) {
                combined.candidate_sets.push_back(option.candidates);
                for(const auto &code:option.candidates)
                    if(std::find(combined.candidates.begin(),
                                 combined.candidates.end(),code)==
                       combined.candidates.end())
                        combined.candidates.push_back(code);
            }
            retained.push_back(std::move(combined));
        } else if(!languageOptions.empty()) {
            retained.push_back(std::move(languageOptions.front()));
        }
        req.choice_groups=std::move(retained);
    }

    std::unordered_set<std::string> inProgress(req.in_progress.begin(),
                                                req.in_progress.end());
    std::unordered_set<std::string> requiredSeen;
    std::unordered_set<std::string> majorSeen;
    std::string neededText;
    bool inNeededList=false;
    bool inMajorSection=false;
    const std::regex choicePattern(
        R"((\d+)\s*(credits?|credit\s+units?|courses?|classes?)\s*(?:in|of|from)\s+(.+))",
        std::regex_constants::icase);
    const std::regex neededCourseLine(
        R"(^\s*[A-Z]{2,5}\s*[0-9]{4}[A-Z]?\b)",
        std::regex_constants::icase);
    auto addNeeded=[&](const std::string &snippet) {
        if(snippet.empty())return;
        std::smatch match;
        if(std::regex_search(snippet,match,choicePattern))return;
        for(const auto &code:courseCodes(snippet)) {
            if(inProgress.count(code))continue;
            if(requiredSeen.insert(code).second)req.required_courses.push_back(code);
            if(inMajorSection&&majorSeen.insert(code).second)
                req.major_courses.push_back(code);
        }
    };
    for(const auto &line:lines) {
        const std::string low=lower(line);
        if(!req.major.empty()&&low.find(lower(req.major))!=std::string::npos&&
           low.find("major")!=std::string::npos)
            inMajorSection=true;
        if(low=="in-progress"||low=="in progress"||
           low.find("gateway education")!=std::string::npos||
           low.find("chinese language")!=std::string::npos||
           low=="legend")
            inMajorSection=false;
        const auto marker=low.find("still needed");
        if(marker!=std::string::npos) {
            const auto colon=line.find(':',marker);
            neededText=colon==std::string::npos?
                trim(line.substr(marker+std::string("still needed").size())):
                trim(line.substr(colon+1));
            if(!neededText.empty()) {
                addNeeded(neededText);
                inNeededList=true;
            }else{
                inNeededList=true;
            }
        }else if(inNeededList){
            if(low=="in-progress"||low=="in progress"||low=="legend"||
               low=="degree audit legend"||low=="end of report"||
               low.find("gateway education")!=std::string::npos||
               low.find("chinese language")!=std::string::npos){
                inNeededList=false;
            }else{
                std::smatch choiceMatch;
                const bool isChoice=std::regex_search(line,choiceMatch,choicePattern);
                if(isChoice||std::regex_search(line,neededCourseLine)){
                    if(!isChoice)
                        addNeeded(line);
                }else{
                    inNeededList=false;
                }
            }
        }
        if(low.find("not be counted")!=std::string::npos||
           low.find("not counted towards")!=std::string::npos||
           low.find("not counted toward")!=std::string::npos) {
            for(const auto &code:courseCodes(line))
                if(std::find(req.non_degree_courses.begin(),
                             req.non_degree_courses.end(),code)==
                   req.non_degree_courses.end())
                    req.non_degree_courses.push_back(code);
        }
    }

    if(req.current_semester.empty()&&!req.conflicting_in_progress_terms&&
       !req.audit_date.empty()) {
        int year=0,month=0,day=0;
        if(parseDate(req.audit_date,year,month,day)) {
            if(month>=8&&month<=12)req.current_semester="Semester A";
            if(month>=1&&month<=5)req.current_semester="Semester B";
            if(month==6||month==7)req.current_semester="Summer";
        }
    }
    return req;
}
