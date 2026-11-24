#include "ui.h"
#include "config.h"
#include "json.h"
#include "page_parser.h"
#include "acquisition.h"
#include "database_builder.h"
#include "course_update_scan.h"
#include "electives.h"
#include "planner.h"
#include "excel.h"
#include "logging.h"
#include "runtime_paths.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <unordered_map>
#include <algorithm>
#include <ctime>
#include <sstream>
#include <cctype>
#include <map>
#include <vector>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

static std::string findPython(const std::filesystem::path &resources);
static std::filesystem::path executablePath(const char*argv0){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const DWORD length=GetModuleFileNameW(nullptr,buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
    if(length>0&&length<buffer.size())return std::filesystem::path(
        std::wstring(buffer.data(),length));
#elif defined(__APPLE__)
    uint32_t size=0;
    _NSGetExecutablePath(nullptr,&size);
    if(size>0){
        std::vector<char> buffer(size+1);
        if(_NSGetExecutablePath(buffer.data(),&size)==0)
            return std::filesystem::weakly_canonical(buffer.data());
    }
#else
    std::vector<char> buffer(4096);
    const ssize_t length=readlink("/proc/self/exe",buffer.data(),buffer.size()-1);
    if(length>0)return std::filesystem::path(std::string(buffer.data(),static_cast<size_t>(length)));
#endif
    std::filesystem::path fallback(argv0?argv0:"");
    if(fallback.is_relative())fallback=std::filesystem::absolute(fallback);
    return fallback;
}
static std::filesystem::path resourceDirectory(const std::filesystem::path&executable){
    const std::filesystem::path executableDirectory=executable.parent_path();
    for(std::filesystem::path candidate=executableDirectory;!candidate.empty();){
        if(std::filesystem::exists(candidate/"config.json")&&
           std::filesystem::exists(candidate/"courses_db.json")&&
           std::filesystem::exists(candidate/"auth_helper.py")&&
           std::filesystem::exists(candidate/"excel_writer.py"))
            return candidate;
        const auto parent=candidate.parent_path();
        if(parent==candidate)break;
        candidate=parent;
    }
    return executableDirectory;
}
static std::string shellQuote(const std::string &value){
#ifdef _WIN32
    std::string quoted="\"";
    for(char ch:value){if(ch=='\"')quoted+='\\';quoted+=ch;}
    return quoted+"\"";
#else
    std::string quoted="'";
    for(char ch:value){if(ch=='\'')quoted+="'\\''";else quoted+=ch;}
    return quoted+"'";
#endif
}
static std::string durationText(long long seconds);

static void printDoctor(const std::filesystem::path &resources,
                        const std::filesystem::path &data){
    std::cout<<"CityU study-plan diagnostic\n";
    std::cout<<"- application resources: "
             <<(std::filesystem::exists(resources/"config.json")&&
                std::filesystem::exists(resources/"courses_db.json")&&
                std::filesystem::exists(resources/"auth_helper.py")&&
                std::filesystem::exists(resources/"excel_writer.py")?"ready":"incomplete")<<"\n";
    std::cout<<"- user data directory: "<<data.string()<<"\n";
    std::cout<<"- saved Degree Works data: "
             <<(std::filesystem::exists(data/"page_text.txt")?"present":"missing")<<"\n";
    std::cout<<"- personal course catalogue: "
             <<(std::filesystem::exists(data/"courses_db.json")?"present":"missing")<<"\n";
    std::cout<<"- Python/openpyxl: ";
#ifdef _WIN32
    const std::string redirect=" >NUL 2>&1";
#else
    const std::string redirect=" >/dev/null 2>&1";
#endif
    const std::string python=shellQuote(findPython(resources));
    const std::string pythonCheck=python+
        " -c \"import openpyxl\""+redirect+" && "+
        python+" "+shellQuote((resources/"auth_helper.py").string())+
        " --check"+redirect;
    if(std::system(pythonCheck.c_str())==0)std::cout<<"ready\n";else std::cout<<"missing\n";
    std::ifstream cooldown(".cityu_cache/catalogue-cooldown-until");
    long long retryAt=0;
    if(cooldown>>retryAt){
        const long long remaining=std::max(0LL,retryAt-static_cast<long long>(std::time(nullptr)));
        std::cout<<"- catalogue cooldown: ";
        if(remaining==0)std::cout<<"expired; a new request may be considered\n";
        else std::cout<<durationText(remaining)<<" remaining; the app will not wait or resume in the background\n";
    }else{
        std::cout<<"- catalogue cooldown: none recorded\n";
    }
}

static std::string findAuthHelper(const std::filesystem::path &resources){
    const auto helper=resources/"auth_helper.py";
    return std::filesystem::exists(helper)?helper.string():"";
}
static std::string findPython(const std::filesystem::path &resources){
    for(auto directory=resources;!directory.empty();){
#ifdef _WIN32
        const auto candidate=directory/".venv"/"Scripts"/"python.exe";
#else
        const auto candidate=directory/".venv"/"bin"/"python";
#endif
        if(std::filesystem::exists(candidate))return candidate.string();
        if(std::filesystem::exists(directory/"CMakeLists.txt")||
           std::filesystem::exists(directory/"vcpkg.json"))break;
        const auto parent=directory.parent_path();
        if(parent==directory)break;
        directory=parent;
    }
#ifdef _WIN32
    return "python";
#else
    return "python3";
#endif
}
static std::string durationText(long long seconds){
    if(seconds<60)return std::to_string(seconds)+" seconds";
    long long minutes=seconds/60;
    long long hours=minutes/60;
    minutes%=60;
    if(hours>0)return std::to_string(hours)+"h "+std::to_string(minutes)+"m";
    return std::to_string(minutes)+"m";
}
static std::string localTimeText(long long timestamp){
    const std::time_t value=static_cast<std::time_t>(timestamp);
    const std::tm *local=std::localtime(&value);
    if(!local)return "unknown local time";
    char text[64];
    if(std::strftime(text,sizeof(text),"%Y-%m-%d %H:%M:%S",local)==0)
        return "unknown local time";
    return text;
}
static std::string upperText(std::string value){
    std::transform(value.begin(),value.end(),value.begin(),
        [](unsigned char ch){return static_cast<char>(std::toupper(ch));});
    return value;
}
static bool parsePositiveInteger(const std::string &value,int &result){
    try{
        size_t used=0;
        const int parsed=std::stoi(value,&used);
        if(used!=value.size()||parsed<=0)return false;
        result=parsed;
        return true;
    }catch(...){return false;}
}
static bool makeDemoRequirements(
        const std::unordered_map<std::string,Course>&courses,
        DegreeRequirements&req){
    std::vector<std::string> eligible;
    for(const auto&[code,course]:courses)
        if(course.catalogue_detail_available&&!course.title.empty()&&
           course.credits_known&&course.credits==3&&!course.semesters.empty()&&
           std::find(course.semesters.begin(),course.semesters.end(),"Not offered")==
               course.semesters.end())
            eligible.push_back(code);
    std::sort(eligible.begin(),eligible.end());

    std::vector<std::string> usable;
    std::unordered_set<std::string> representedSubjects;
    for(const auto&code:eligible){
        const auto firstDigit=code.find_first_of("0123456789");
        if(firstDigit==std::string::npos||firstDigit==0)continue;
        const std::string subject=code.substr(0,firstDigit);
        if(representedSubjects.insert(subject).second)
            usable.push_back(code);
    }
    if(usable.size()<8)return false;
    req.illustrative=true;
    ChoiceGroup group;
    group.required_credits=3;
    group.candidates.assign(usable.begin(),usable.begin()+8);
    req.choice_groups.push_back(std::move(group));
    return true;
}
static std::string missingCourseData(const Course*course,const std::string&code,
                                     bool inProgress=false){
    if(!course)return "Trying to get information for course "+code;
    std::vector<std::string> missing;
    if(course->title.empty())missing.push_back("title");
    if(!course->credits_known)missing.push_back("credit units");
    if(!inProgress&&course->semesters.empty())
        missing.push_back("offering information");
    if(missing.empty())return "Course "+code+" is not schedulable from verified data";
    const bool offeringMissing=std::find(missing.begin(),missing.end(),"offering information")!=
                               missing.end();
    missing.erase(std::remove(missing.begin(),missing.end(),"offering information"),missing.end());
    std::string result=offeringMissing||!course->catalogue_detail_available
        ?"Trying to get information for course "+code
        :"Missing ";
    if(missing.empty())return result;
    if(offeringMissing||!course->catalogue_detail_available)result+=" (missing ";
    for(size_t i=0;i<missing.size();++i){
        if(i)result+=", ";
        result+=missing[i];
    }
    if(offeringMissing||!course->catalogue_detail_available)result+=")";
    return result;
}
static std::string prerequisiteSummary(const Course&course,
                                      const DegreeRequirements&req){
    if(course.prerequisite_source=="UNKNOWN"||
       std::find(course.prerequisites.begin(),course.prerequisites.end(),"UNKNOWN")!=
           course.prerequisites.end())
        return {};
    if(course.prerequisites.empty()){
        if(!course.prerequisites_text.empty())
            return "Prerequisites: "+course.prerequisites_text;
        return "Prerequisites: None";
    }
    std::unordered_set<std::string> completed(req.completed_courses.begin(),
                                               req.completed_courses.end());
    std::unordered_set<std::string> studying(req.in_progress.begin(),
                                              req.in_progress.end());
    std::unordered_set<std::string> scheduled(req.required_courses.begin(),
                                               req.required_courses.end());
    scheduled.insert(req.major_courses.begin(),req.major_courses.end());
    for(const auto&group:req.choice_groups)
        for(const auto&candidate:group.candidates)scheduled.erase(candidate);
    std::string result="Prerequisites: ";
    for(size_t i=0;i<course.prerequisites.size();++i){
        if(i)result+=", ";
        const auto&code=course.prerequisites[i];
        result+=code+" ";
        if(completed.count(code))result+="✅ studied";
        else if(studying.count(code))result+="🔄 studying";
        else if(scheduled.count(code))result+="📅 planned";
        else result+="⚠️ not listed";
    }
    return result;
}

int main(int argc,char**argv){
    bool doctor=false,dry=false,refresh=false,demo=false;
    bool databaseMode=false,databaseImportOnly=false,databaseFetchOnly=false;
    bool databasePathSpecified=false;
    bool scanCourseUpdates=false;
    std::string databasePath="CityuCources.txt";
    AcquisitionMode mode=AcquisitionMode::STATIC_FIRST;
    bool modeSpecified=false;
    for(int i=1;i<argc;++i){
        std::string a=argv[i];
        if(a=="--doctor")doctor=true;
        else if(a=="--dry-run")dry=true;
        else if(a=="--refresh"||a=="--no-cache")refresh=true;
        else if(a=="--demo")demo=true;
        else if(a=="--database"){
            databaseMode=true;
            if(i+1<argc&&std::string(argv[i+1]).rfind("--",0)!=0){
                databasePath=argv[++i];
                databasePathSpecified=true;
            }
        }
        else if(a=="--import-only")databaseImportOnly=true;
        else if(a=="--fetch-only")databaseFetchOnly=true;
        else if(a=="--mode"&&i+1<argc){
            const std::string value=argv[++i];
            if(value!="1"&&value!="2"){std::cerr<<"Mode must be 1 (start planning) or 2 (scan Degree Works course updates).\n";return 2;}
            scanCourseUpdates=value=="2";
            mode=AcquisitionMode::STATIC_FIRST;
            modeSpecified=true;
        }
        else if(a=="--help"){std::cout<<"Usage: cityu_study_plan [--mode 1|2] [--dry-run] [--refresh] [--doctor] [--demo]\n"
            "       cityu_study_plan --database [pasted-course-text.txt] [--import-only]\n"
            "       cityu_study_plan --database --fetch-only\n"
            "  --demo writes an offline workbook from bundled catalogue records; add --dry-run to print the plan only.\n"
            "  --mode 2 refreshes only course codes listed in Degree Works, then continues to planning.\n"
            "  --database imports course text and optionally fills missing entries with sequential official-catalogue requests.\n"
            "  --import-only skips all network access.\n"
            "  --fetch-only fills missing entries in the existing courses_db.json without importing a source file.\n";return 0;}
    }
    std::error_code pathError;
    if(databaseMode){
        databasePath=std::filesystem::absolute(databasePath,pathError).string();
        if(pathError){std::cerr<<"Could not resolve the database source path: "<<pathError.message()<<"\n";return 2;}
    }
    const auto resources=resourceDirectory(executablePath(argc>0?argv[0]:nullptr));
    if(demo&&scanCourseUpdates){
        std::cerr<<"The offline demo has no Degree Works audit to scan.\n";
        return 2;
    }
    const auto dataDirectory=userDataDirectory();
    if(dataDirectory.empty()||dataDirectory.is_relative()){
        std::cerr<<"Could not determine an absolute per-user data directory from the operating-system environment.\n";
        return 2;
    }
    std::filesystem::create_directories(dataDirectory,pathError);
    if(pathError){
        std::cerr<<"Could not create the user data directory "<<dataDirectory.string()
                 <<": "<<pathError.message()<<"\n";
        return 2;
    }
    std::filesystem::current_path(dataDirectory,pathError);
    if(pathError){
        std::cerr<<"Could not open the user data directory "<<dataDirectory.string()
                 <<": "<<pathError.message()<<"\n";
        return 2;
    }
    const std::filesystem::path scannedDatabasePath="courses_db_scanned.json";
    pathError.clear();
    bool scannedDatabaseExists=
        std::filesystem::exists(scannedDatabasePath,pathError);
    if(pathError){
        std::cerr<<"Could not check the scanned course database path: "
                 <<pathError.message()<<"\n";
        return 2;
    }
    if(doctor){printDoctor(resources,dataDirectory);return 0;}
    if(!demo&&!std::filesystem::exists(resources/"config.json")){
        std::cerr<<"Application configuration is missing beside the executable or its parent directories.\n";
        return 2;
    }
    if(!demo&&!std::filesystem::exists(resources/"courses_db.json")){
        std::cerr<<"The bundled course catalogue is missing beside the executable or its parent directories.\n";
        return 2;
    }
    if(!demo&&!std::filesystem::exists(resources/"auth_helper.py")){
        std::cerr<<"The authentication helper is missing beside the executable or its parent directories.\n";
        return 2;
    }
    if(demo&&!std::filesystem::exists(resources/"courses_db.json")){
        std::cerr<<"The bundled course catalogue is missing; the real-database demo cannot run.\n";
        return 2;
    }
    if(!demo&&!std::filesystem::exists(resources/"excel_writer.py")){
        std::cerr<<"The Excel writer is missing beside the executable or its parent directories.\n";
        return 2;
    }
    if(!demo&&!std::filesystem::exists("courses_db.json")){
        std::filesystem::copy_file(resources/"courses_db.json","courses_db.json",pathError);
        if(pathError){
            std::cerr<<"Could not initialize the personal course catalogue: "<<pathError.message()<<"\n";
            return 2;
        }
    }
    if(!demo){
        std::string catalogueError;
        if(!mergeBundledCourseDb((resources/"courses_db.json").string(),
                                 "courses_db.json",catalogueError)){
            std::cerr<<"Could not synchronize bundled course updates into the personal catalogue: "
                     <<catalogueError<<"\n";
            return 2;
        }
    }
    Config config;std::string configError;
    if(!loadConfig((resources/"config.json").string(),config,configError)){
        std::cerr<<"Could not load application configuration: "<<configError<<"\n";
        return 2;
    }
    if((databaseImportOnly||databaseFetchOnly)&&!databaseMode){
        std::cerr<<"--import-only and --fetch-only must be used with --database.\n";
        return 2;
    }
    if(databaseImportOnly&&databaseFetchOnly){
        std::cerr<<"Use either --import-only or --fetch-only, not both.\n";
        return 2;
    }
    if(databaseFetchOnly&&databasePathSpecified){
        std::cerr<<"--fetch-only reads courses_db.json; do not pass a text file path.\n";
        return 2;
    }
    if(databaseMode){
        if(demo){std::cerr<<"Use --database or --demo, not both.\n";return 2;}
        return runDatabaseBuilder(databasePath,databaseImportOnly,databaseFetchOnly,config);
    }
    showStartScreen();
    if(demo)    printInfo("Offline demo: catalogue example only, not a degree requirement; no student data is used.");

    if(!demo){
        printSection("Choose how course information is found");
        std::cout<<"  1) Start planning! — use local course data first and look up missing details when needed.\n";
        std::cout<<"  2) Scan for course updates — refresh catalogue details only for course codes listed in Degree Works, then return here.\n";
        printInfo("Scanning first reads your Degree Works course list; login may be required. It does not choose electives or create a workbook.");
        printInfo("Press Ctrl+C at any time to stop. Completed course updates are saved; you can run the scan again later.");
    }
    if(!modeSpecified&&!demo){
        while(true){
            const std::string selection=readPrompt("Enter 1 to start planning or 2 to scan course updates");
            const std::string value=selection.empty()?"":std::string(selection);
            if(value=="1"){mode=AcquisitionMode::STATIC_FIRST;break;}
            if(value=="2"){scanCourseUpdates=true;mode=AcquisitionMode::STATIC_FIRST;break;}
            printWarning("Choose 1 to start planning or 2 to scan course updates.");
        }
    }
    if(!demo&&!scanCourseUpdates)
        printInfo("Selected: Start planning. Local course data is preferred.");

    if(!demo&&(!std::filesystem::exists("session_state.json")||!std::filesystem::exists("page_text.txt"))){
        if(!privacyConfirm()){std::cout<<"Cancelled.\n";return 130;}
        std::string helper=findAuthHelper(resources);
        std::string python=findPython(resources);
        if(helper.empty()){std::cout<<"Authentication helper is missing. It was not sent to FIX.\n";return 21;}
        std::string preflight=shellQuote(python)+" -c \"import openpyxl\"";
        if(std::system(preflight.c_str())!=0){
            std::cout<<"Python dependency openpyxl is missing for "<<python<<".\n";
#ifdef _WIN32
            std::cout<<"Create .venv with `py -m venv .venv`, install dependencies using `.venv\\Scripts\\python.exe -m pip install playwright openpyxl`, then install Chromium using `.venv\\Scripts\\python.exe -m playwright install chromium`.\n";
#else
            std::cout<<"Create .venv with `python3 -m venv .venv`, install dependencies using `.venv/bin/python -m pip install playwright openpyxl`, then install Chromium using `.venv/bin/python -m playwright install chromium`.\n";
#endif
            return 21;
        }
        const std::string playwrightCheck=shellQuote(python)+" "+
            shellQuote(helper)+" --check";
        if(std::system(playwrightCheck.c_str())!=0)return 21;
        std::string command=shellQuote(python)+" "+shellQuote(helper)+
            " --state "+shellQuote((dataDirectory/"session_state.json").string())+
            " --page-text "+shellQuote((dataDirectory/"page_text.txt").string())+
            " --courses-db "+shellQuote((dataDirectory/"courses_db.json").string());
        int rc=std::system(command.c_str());
        if(rc!=0){std::cout<<"Authentication/session could not be completed. It was not sent to FIX. Review the helper output and retry.\n";return 21;}
    }
    DegreeRequirements req;
    std::vector<std::string> auditScanCodes;
    if(!demo){
        if(!std::filesystem::exists("page_text.txt")){printWarning("page_text.txt is missing. Degree Works data could not be read.");return 22;}
        std::ifstream pf("page_text.txt");std::string page((std::istreambuf_iterator<char>(pf)),std::istreambuf_iterator<char>());
        req=parseDegreeWorks(page);
        auditScanCodes=degreeWorksCourseCodes(req);
    }
    std::unordered_map<std::string,Course> db;
    std::string databaseError;
    const auto courseDbPath=demo?resources/"courses_db.json":std::filesystem::path("courses_db.json");
    if(std::filesystem::exists(courseDbPath)&&!loadCoursesDb(courseDbPath.string(),db,databaseError)){
        std::cerr<<"Could not load course database "<<courseDbPath.string()<<": "
                 <<databaseError<<"\n";
        return 2;
    }
    if(!demo&&scannedDatabaseExists){
        std::unordered_map<std::string,Course> scannedCourses;
        if(!loadCoursesDb(scannedDatabasePath.string(),scannedCourses,databaseError)){
            std::cerr<<"Could not load scanned course database "
                     <<scannedDatabasePath.string()<<": "<<databaseError<<"\n";
            return 2;
        }
        for(auto&[code,course]:scannedCourses){
            const auto fallback=db.find(code);
            if(fallback!=db.end())
                course=mergeCourseData(course,fallback->second);
            if(course.code.empty())course.code=code;
            db[code]=std::move(course);
        }
    }
    std::unordered_map<std::string,std::string> gatewayAreas;
    if(!loadGatewayEducationAreas((resources/"ge_areas.json").string(),
                                  gatewayAreas,databaseError)){
        std::cerr<<"Could not load Gateway Education area data: "<<databaseError<<"\n";
        return 2;
    }
    for(auto&[code,course]:db){
        const auto area=gatewayAreas.find(code);
        if(area!=gatewayAreas.end())course.area=area->second;
    }
    if(demo&&!makeDemoRequirements(db,req)){
        std::cerr<<"The bundled course catalogue does not contain enough complete entries for the offline demo.\n";
        return 2;
    }
    std::unordered_map<std::string,Course> courses=db;
    std::unordered_map<std::string,Course> chosen;
    for(const auto&[code,area]:gatewayAreas){
        if(courses.find(code)==courses.end()){
            Course placeholder;
            placeholder.code=code;
            placeholder.area=area;
            courses.emplace(code,std::move(placeholder));
        }
    }
    for(auto&group:req.choice_groups){
        if(group.areas.empty())continue;
        for(const auto&[code,course]:courses)
            if(courseMatchesArea(course,group.areas)&&
               course.catalogue_detail_available&&
               hasUsableChoiceCourse(course,group.required_credits>0)&&
               std::find(group.candidates.begin(),group.candidates.end(),code)==
                   group.candidates.end())
                group.candidates.push_back(code);
        std::sort(group.candidates.begin(),group.candidates.end());
        if(!canSatisfyChoiceGroup(group,group.candidates,courses)){
            for(int area:group.areas){
                int knownCredits=0;
                for(const auto&code:group.candidates){
                    const auto course=courses.find(code);
                    if(course!=courses.end()&&courseMatchesArea(course->second,{area}))
                        knownCredits+=electiveCredits(course->second);
                }
                if(knownCredits>=group.minimum_per_area_credits)continue;
                int added=0;
                for(const auto&[code,courseArea]:gatewayAreas){
                    if(courseArea!="Area "+std::to_string(area)||
                       std::find(group.candidates.begin(),group.candidates.end(),code)!=
                           group.candidates.end())
                        continue;
                    group.candidates.push_back(code);
                    if(++added>=3)break;
                }
            }
        }
    }
    const auto ensureCourseRecord=[&](const std::string&code){
        if(courses.find(code)!=courses.end())return;
        Course placeholder;
        placeholder.code=code;
        const auto area=gatewayAreas.find(code);
        if(area!=gatewayAreas.end())placeholder.area=area->second;
        courses.emplace(code,std::move(placeholder));
    };
    for(const auto&code:req.required_courses)ensureCourseRecord(code);
    for(const auto&code:req.in_progress)ensureCourseRecord(code);
    for(const auto&group:req.choice_groups)
        for(const auto&code:group.candidates)ensureCourseRecord(code);
    std::vector<std::string> needed=req.in_progress;
    needed.insert(needed.end(),req.required_courses.begin(),req.required_courses.end());
    for(const auto&g:req.choice_groups)needed.insert(needed.end(),g.candidates.begin(),g.candidates.end());
    std::sort(needed.begin(),needed.end());
    needed.erase(std::unique(needed.begin(),needed.end()),needed.end());
    std::unordered_set<std::string> scannedCourseCodes;
    if(scanCourseUpdates){
        scannedCourseCodes=scanDegreeWorksCourseUpdates(
            auditScanCodes,scannedDatabasePath,config,courses,gatewayAreas);
        pathError.clear();
        scannedDatabaseExists=
            std::filesystem::exists(scannedDatabasePath,pathError);
        if(pathError){
            std::cerr<<"Could not check the scanned course database after scanning: "
                     <<pathError.message()<<"\n";
            return 2;
        }
        printInfo("Scan finished. Planning has not started yet.");
        while(readPrompt("Type 1 to continue to study planning")!="1")
            printWarning("Enter 1 to continue to planning. Press Ctrl+C to stop the program.");
    }
    Acquisition acq(config,mode);
    if(!demo){
        std::string acquisitionDatabaseError;
        const auto primaryPath=scannedDatabaseExists
            ?scannedDatabasePath:std::filesystem::path("courses_db.json");
        if(!acq.loadStaticDb(primaryPath.string(),acquisitionDatabaseError)){
            std::cerr<<"Could not load the primary course database for planning: "
                     <<acquisitionDatabaseError<<"\n";
            return 2;
        }
        if(primaryPath==scannedDatabasePath&&
           !acq.loadFallbackDb("courses_db.json",acquisitionDatabaseError)){
            std::cerr<<"Could not load the static fallback course database: "
                     <<acquisitionDatabaseError<<"\n";
            return 2;
        }
    }
    std::vector<std::string> lookupNeeded;
    std::unordered_set<std::string> assumedOfferingOnly;
    for(const auto&code:needed){
        if(scannedCourseCodes.count(code))continue;
        const auto course=courses.find(code);
        const bool offeringOnlyMissing=course!=courses.end()&&
            !course->second.title.empty()&&course->second.credits_known&&
            course->second.catalogue_detail_available&&
            course->second.prerequisite_source!="UNKNOWN"&&
            course->second.semesters.empty();
        if(!demo&&mode==AcquisitionMode::STATIC_FIRST&&!refresh&&offeringOnlyMissing){
            assumedOfferingOnly.insert(code);
            continue;
        }
        lookupNeeded.push_back(code);
    }
    const std::vector<std::string> networkLookupCodes=demo
        ?std::vector<std::string>{}
        :acq.pendingNetworkFetchCodes(lookupNeeded,refresh);
    const std::unordered_set<std::string> networkLookupSet(
        networkLookupCodes.begin(),networkLookupCodes.end());
    const std::size_t networkCount=networkLookupCodes.size();
    if(demo){
        printSection("Offline demo");
        printInfo("Using the bundled catalogue; no network lookups will be made.");
    }else if(networkCount==0){
        printSection("Course data");
        printSuccess("No online catalogue requests are pending.");
    }else{
        printSection("Course lookup estimate");
        printInfo("Catalogue lookups needed: "+std::to_string(networkCount));
        const long long interval=std::max(0,config.min_request_interval_ms);
        const long long jitter=std::max(0,config.jitter_ms);
        const long long oneAttemptMin=static_cast<long long>(networkCount>0?networkCount-1:0)*interval/1000;
        const long long oneAttemptMax=static_cast<long long>(networkCount)*30+
            static_cast<long long>(networkCount>0?networkCount-1:0)*(interval+jitter)/1000;
        const long long attempts=static_cast<long long>(networkCount)*std::max(1,config.max_retries);
        long long backoff=0;
        for(int i=0;i<std::max(0,config.max_retries-1);++i)
            backoff+=(1LL<<std::min(i,20));
        const long long retryMax=attempts*30+
            std::max(0LL,attempts-1)*(interval+jitter)/1000+
            static_cast<long long>(networkCount)*backoff;
        printInfo("Estimated time with one attempt per course: about "+
                  durationText(oneAttemptMin)+"–"+durationText(oneAttemptMax)+".");
        printInfo("Maximum with configured retries: about "+
                  durationText(retryMax)+"; a site block stops further requests.");
        printWarning("Estimates are approximate; catalogue access is not guaranteed.");
    }
    bool blockReported=false;
    if(!demo&&acq.catalogueCoolingDown()&&networkCount>0){
        const auto missing=acq.unavailableWithoutNetwork(needed,refresh);
        const long long remaining=acq.cooldownRemainingSeconds();
        printWarning("Catalogue cooldown remains for about "+durationText(remaining)+
                     " (until approximately "+localTimeText(static_cast<long long>(std::time(nullptr))+remaining)+").");
        printInfo("Using local catalogue data only; "+std::to_string(missing.size())+
                  " course records remain incomplete.");
        blockReported=true;
    }
    bool lookupsCancelled=false;
    if(networkCount>0&&!blockReported){
        std::string proceed=readPrompt("Type OK to begin these lookups");
        if(upperText(proceed)!="OK"){
            printWarning("Course lookups were cancelled; continuing with local data and workbook warnings.");
            lookupsCancelled=true;
        }
    }
    size_t networkProgress=0;
    for(const auto&code:needed){
        if(networkCount>0&&!blockReported&&!lookupsCancelled&&
           networkLookupSet.count(code)){
            ++networkProgress;
            printInfo("Checking course "+std::to_string(networkProgress)+" of "+
                      std::to_string(networkCount)+": "+code);
        }
        AcquisitionResult ar;
        if(demo){
            ar.course=db.at(code);
        }else if(assumedOfferingOnly.count(code)){
            ar.course=courses.at(code);
        }else if(scannedCourseCodes.count(code)){
            ar.course=courses.at(code);
        }else if(lookupsCancelled){
            const auto local=courses.find(code);
            if(local!=courses.end())ar.course=local->second;
            else ar.course.code=code;
        }else{
            ar=acq.fetch(code,refresh);
        }
        if(!ar.database_save_error.empty()){
            printWarning("Course data was read but could not be added to courses_db.json: "+ar.database_save_error);
            writeErrorLog("DB-SAVE",ar.database_save_error,"Check write permissions for the course database.",code);
        }
        if(ar.needs_fix){
            writeErrorLog(ar.error_code,ar.plain_error,"Supply a correct URL for this course or try --refresh later.",code);
            if(ar.site_blocked){
                if(!blockReported){
                    printWarning("Catalogue access is blocked or challenged. Stopping further requests; continuing with available local data only.");
                    blockReported=true;
                }
            }else{
                printWarning(code+" needs a course-specific correction: "+ar.plain_error);
                std::string url=readPrompt("Enter a verified catalogue URL, or press Enter to leave this course unresolved");
                if(url.empty()){printWarning("No URL supplied; course remains unresolved.");continue;}
                std::ofstream ov("overrides.json",std::ios::app);
                std::time_t now=std::time(nullptr);std::tm tm=*std::localtime(&now);char stamp[32];std::strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%S",&tm);
                ov<<"{\"course\":\""<<code<<"\",\"url\":\""<<url<<"\",\"timestamp\":\""<<stamp<<"\"}\n";
                writeErrorLog("FIX-OVERRIDE","An override URL was saved for "+code,"The override will be reused automatically.","URL: "+url);
                ar.course.url=url;
            }
        }
        courses[code]=ar.course;
        const auto area=gatewayAreas.find(code);
        if(area!=gatewayAreas.end())courses[code].area=area->second;
        if(std::find(req.required_courses.begin(),req.required_courses.end(),code)!=req.required_courses.end())
            chosen[code]=courses[code];
    }
    for(const auto&entry:req.in_progress_details){
        Course&course=courses[entry.code];
        course.code=entry.code;
        if(!entry.title.empty())course.title=entry.title;
        if(entry.credits_known){
            course.credits=entry.credits;
            course.credits_known=true;
        }
    }
    std::vector<ChoiceGroup> unresolvedChoiceGroups;
    for(const auto&group:req.choice_groups){
        if(group.areas.empty()&&group.candidates.size()==1){
            const std::string&code=group.candidates.front();
            const auto course=courses.find(code);
            const bool exactSingleCourse=group.choose==1||
                (group.required_credits>0&&course!=courses.end()&&
                 electiveCredits(course->second)==group.required_credits);
            if(exactSingleCourse&&course!=courses.end()){
                if(std::find(req.required_courses.begin(),req.required_courses.end(),code)==
                   req.required_courses.end())
                    req.required_courses.push_back(code);
                chosen[code]=course->second;
                continue;
            }
        }
        unresolvedChoiceGroups.push_back(group);
    }
    req.choice_groups=std::move(unresolvedChoiceGroups);
    classifyPrerequisites(req,courses);

    std::unordered_set<std::string> inProgress(req.in_progress.begin(),req.in_progress.end());
    std::vector<std::string> incompleteRequired;
    for(const auto&code:req.required_courses){
        const auto course=courses.find(code);
        const bool current=inProgress.count(code)>0;
        if(course==courses.end()||course->second.title.empty()||
           !course->second.credits_known)
            incompleteRequired.push_back(
                missingCourseData(course==courses.end()?nullptr:&course->second,code,current));
    }
    if(!incompleteRequired.empty()){
        printSection("Course data check");
        printWarning("Some required-course details are unavailable; the workbook will include them with warnings:");
        for(const auto&item:incompleteRequired)std::cout<<"  "<<item<<"\n";
        printInfo("Verify these entries with the official course catalogue before relying on the schedule.");
    }

    std::vector<std::vector<std::string>> eligibleChoiceGroups;
    std::vector<bool> feasibleChoiceGroups;
    eligibleChoiceGroups.reserve(req.choice_groups.size());
    feasibleChoiceGroups.reserve(req.choice_groups.size());
    for(size_t groupIndex=0;groupIndex<req.choice_groups.size();++groupIndex){
        const auto&group=req.choice_groups[groupIndex];
        if(!group.areas.empty()&&group.candidates.empty()){
            printSection("Choice data check");
            printWarning("Degree Works requires credits from Gateway Education areas, but no verified candidates are available.");
            printInfo("The workbook will show this choice as unresolved; add verified area data before relying on the plan.");
            eligibleChoiceGroups.emplace_back();
            feasibleChoiceGroups.push_back(false);
            continue;
        }
        std::vector<std::string> eligible;
        std::vector<std::string> incomplete;
        for(const auto&code:group.candidates){
            const auto course=courses.find(code);
            if(course!=courses.end()&&
               hasUsableChoiceCourse(course->second,group.required_credits>0)&&
               (group.areas.empty()||courseMatchesArea(course->second,group.areas))){
                eligible.push_back(code);
                if(course->second.title.empty()||!course->second.credits_known||
                   !course->second.catalogue_detail_available)
                    incomplete.push_back(missingCourseData(&course->second,code));
            }else{
                incomplete.push_back(
                    missingCourseData(course==courses.end()?nullptr:&course->second,code));
            }
        }
        const bool feasible=canSatisfyChoiceGroup(group,eligible,courses);
        if(!feasible){
            printSection("Choice data check");
            if(group.required_credits>0)
                printWarning("Available verified course records cannot meet this Degree Works choice of "+
                             std::to_string(group.required_credits)+" credits.");
            else
                printWarning("Available verified course records cannot meet this Degree Works choice of "+
                             std::to_string(group.choose)+" courses.");
            if(!incomplete.empty()){
                printInfo("Incomplete candidates:");
                for(const auto&item:incomplete)std::cout<<"  "<<item<<"\n";
            }
            if(blockReported&&mode==AcquisitionMode::STATIC_FIRST)
                printInfo("Online lookup was skipped because the catalogue cooldown is active. Retry after it expires.");
            printInfo("The workbook will include this choice as unresolved.");
        }else if(!incomplete.empty()){
            printSection("Catalogue alternatives");
            printWarning("Some Degree Works alternatives have incomplete details:");
            for(const auto&item:incomplete)std::cout<<"  "<<item<<"\n";
            printInfo("Candidates with missing details remain selectable; missing credits default to 3.");
        }
        eligibleChoiceGroups.push_back(std::move(eligible));
        feasibleChoiceGroups.push_back(feasible);
    }

    bool advanced=req.entry_status=="Advanced Standing I"||
                  req.entry_status=="Advanced Standing II";
    if(!demo&&req.remaining_years==0&&!advanced){
        while(true){
            const std::string entry=readPrompt(
                "Entry type: 1 = Year 1, 2 = Advanced Standing I or II");
            if(entry=="1"){req.remaining_years=4;break;}
            if(entry=="2"){advanced=true;req.remaining_years=3;break;}
            printWarning("Enter 1 or 2.");
        }
    }
    if(!demo&&req.current_semester.empty()&&req.in_progress.empty()){
        while(true){
            const std::string term=readPrompt(
                "Current semester: 1 = Semester A, 2 = Semester B, 3 = Summer");
            if(term=="1"){req.current_semester="Semester A";break;}
            if(term=="2"){req.current_semester="Semester B";break;}
            if(term=="3"){req.current_semester="Summer";break;}
            printWarning("Enter 1, 2, or 3.");
        }
    }else if(req.current_semester.empty()){
        req.current_semester="Semester A";
    }

    std::vector<std::vector<std::string>> preferences;
    if(std::any_of(feasibleChoiceGroups.begin(),feasibleChoiceGroups.end(),
                   [](bool feasible){return feasible;})){
        const std::vector<std::pair<std::string,std::vector<std::string>>> preferenceQuestions={
            {"Topics you do not prefer",{"law","religion/philosophy","economics/finance","management","media/communication","music/art","engineering","language","psychology","sociology","history/culture","science/technology","arts/humanities","social/business","none / all fine"}},
            {"Assessment types you do not prefer",{"100% continuous assessment","group project","presentation","none / all fine"}}
        };
        preferences.resize(preferenceQuestions.size());
        size_t question=0;
        while(!demo&&question<preferenceQuestions.size()){
            if(question==1)
                printInfo("Exam and continuous assessment are reported separately.");
            auto answer=readMultiSelect(preferenceQuestions[question].first,
                                       preferenceQuestions[question].second,question>0);
            if(answer.previous){
                --question;
                continue;
            }
            preferences[question]=std::move(answer.selections);
            ++question;
        }
    }

    ElectivePickerResult picker = selectElectives(
        req, courses, chosen, eligibleChoiceGroups, feasibleChoiceGroups,
        preferences, demo);
    std::vector<ElectiveSelection> electiveSelections =
        std::move(picker.selections);
    std::vector<std::vector<std::string>> rankedChoiceGroups =
        std::move(picker.ranked_groups);
    Plan plan=makePlan(req,courses,chosen,config,advanced);
    const int preferredWorkload=advanced?config.advanced_standing_max_credits:
        std::min(18,config.default_max_credits);
    const int workloadTarget=std::max(3,
        std::min(plan.average_credits_per_term,preferredWorkload));
    const int semesterMaximum=advanced?config.advanced_standing_max_credits:
        config.default_max_credits;
    std::map<std::string,int> termCredits;
    for(const auto&course:plan.courses){
        if(course.manual_placement||course.requirement=="In progress")continue;
        termCredits[course.year_semester]+=course.first_term_credits;
        if(!course.second_year_semester.empty())
            termCredits[course.second_year_semester]+=course.second_term_credits;
    }
    std::vector<std::string> overloadedTerms;
    for(const auto&[term,credits]:termCredits)
        if(credits>workloadTarget)overloadedTerms.push_back(term);
    if(!demo&&!overloadedTerms.empty()){
        printSection("Elective workload review");
        printWarning("A semester exceeds the preferred workload target. Review the electives before keeping this schedule.");
        for(const auto&term:overloadedTerms)
            printInfo(term+": "+std::to_string(termCredits[term])+
                " credits (target "+std::to_string(workloadTarget)+
                ", maximum "+std::to_string(semesterMaximum)+").");

        std::vector<std::vector<std::string>> alternatives(req.choice_groups.size());
        std::unordered_set<std::string> currentlyChosen;
        for(const auto&selection:electiveSelections)
            currentlyChosen.insert(selection.course_codes.begin(),
                                   selection.course_codes.end());
        for(size_t groupIndex=0;groupIndex<req.choice_groups.size();++groupIndex){
            if(electiveSelections[groupIndex].course_codes.empty())continue;
            for(const auto&code:rankedChoiceGroups[groupIndex]){
                if(currentlyChosen.count(code))continue;
                const auto course=courses.find(code);
                if(course==courses.end())continue;
                const bool opensElsewhere=std::any_of(overloadedTerms.begin(),
                    overloadedTerms.end(),[&](const std::string&term){
                        const std::string other=term.find("Semester A")!=std::string::npos
                            ?"Semester B":"Semester A";
                        return hasOfferingInTerm(course->second,other);
                    });
                if(opensElsewhere)
                    alternatives[groupIndex].push_back(code);
            }
            if(!alternatives[groupIndex].empty()){
                printInfo("Choice "+std::to_string(groupIndex+1)+
                    " alternatives in another semester:");
                for(const auto&code:alternatives[groupIndex]){
                    const auto&course=courses.at(code);
                    const auto position=std::find(rankedChoiceGroups[groupIndex].begin(),
                                                  rankedChoiceGroups[groupIndex].end(),code);
                    std::cout<<"  "<<(position-rankedChoiceGroups[groupIndex].begin()+1)
                             <<") "<<code;
                    if(!course.title.empty())std::cout<<" — "<<course.title;
                    std::cout<<" ("<<electiveCredits(course)<<" credits"
                             <<(course.credits_known?"":", assumed")<<")\n"
                             <<"     "<<(course.url.empty()
                                ?"https://www.cityu.edu.hk/catalogue/ug/current/course/"+code+".htm"
                                :course.url)<<"\n";
                }
            }
        }
        if(yesNo("Change any selected electives")){
            for(size_t groupIndex=0;groupIndex<req.choice_groups.size();++groupIndex){
                if(alternatives[groupIndex].empty()||
                   electiveSelections[groupIndex].course_codes.empty())continue;
                const auto&group=req.choice_groups[groupIndex];
                std::unordered_set<std::string> allowed(
                    alternatives[groupIndex].begin(),alternatives[groupIndex].end());
                allowed.insert(electiveSelections[groupIndex].course_codes.begin(),
                               electiveSelections[groupIndex].course_codes.end());
                std::string prompt="Choice "+std::to_string(groupIndex+1)+
                    " replacement course numbers (comma-separated, or Enter to keep)";
                while(true){
                    const std::string input=readPrompt(prompt);
                    if(input.empty())break;
                    std::vector<std::string> replacement;
                    std::istringstream inputStream(input);
                    std::string token;
                    bool valid=true;
                    while(std::getline(inputStream,token,',')){
                        token.erase(0,token.find_first_not_of(" \t"));
                        while(!token.empty()&&std::isspace(
                            static_cast<unsigned char>(token.back())))token.pop_back();
                        int row=0;
                        if(!parsePositiveInteger(token,row)||
                           static_cast<size_t>(row)>rankedChoiceGroups[groupIndex].size()){
                            valid=false;
                            break;
                        }
                        const std::string&code=
                            rankedChoiceGroups[groupIndex][static_cast<size_t>(row-1)];
                        if(!allowed.count(code)||
                           std::find(replacement.begin(),replacement.end(),code)!=replacement.end()){
                            valid=false;
                            break;
                        }
                        replacement.push_back(code);
                    }
                    if(valid)
                        valid=validateChoiceSelection(group,replacement,courses).empty();
                    if(!valid){
                        printWarning("That selection does not meet the Degree Works credit, count, or area requirement.");
                        continue;
                    }
                    for(const auto&code:electiveSelections[groupIndex].course_codes)
                        if(std::find(req.required_courses.begin(),req.required_courses.end(),code)==
                               req.required_courses.end()&&
                           std::find(req.major_courses.begin(),req.major_courses.end(),code)==
                               req.major_courses.end())
                            chosen.erase(code);
                    electiveSelections[groupIndex].course_codes=replacement;
                    for(const auto&code:replacement)chosen[code]=courses.at(code);
                    currentlyChosen.insert(replacement.begin(),replacement.end());
                    break;
                }
            }
            plan=makePlan(req,courses,chosen,config,advanced);
            termCredits.clear();
            for(const auto&course:plan.courses){
                if(course.manual_placement||course.requirement=="In progress")continue;
                termCredits[course.year_semester]+=course.first_term_credits;
                if(!course.second_year_semester.empty())
                    termCredits[course.second_year_semester]+=course.second_term_credits;
            }
        }
        for(const auto&term:overloadedTerms){
            if(termCredits[term]>semesterMaximum)
                printWarning(term+" remains above the maximum credit limit; adjust the schedule manually.");
            else if(termCredits[term]>workloadTarget)
                printInfo(term+" remains above the preferred workload target.");
        }
    }
    plan.elective_selections=std::move(electiveSelections);
    if(lookupsCancelled)
        plan.warnings.push_back(
            "Catalogue lookups were cancelled; verify course details and the resulting placements before registration.");
    else if(blockReported)
        plan.warnings.push_back(
            "Catalogue access was unavailable; local records or assumptions were used where details were missing.");
    if(!plan.warnings.empty()){
        printSection("Planning warnings");
        for(const auto&warning:plan.warnings)printWarning(warning);
    }
    if(dry){
        printSection(demo?"Offline demo plan":"Dry-run plan");
        printInfo("No Excel file will be written.");
        std::map<std::string,std::vector<std::pair<const PlanCourse*,int>>> byTerm;
        for(const auto&course:plan.courses){
            byTerm[course.manual_placement
                ?"Manual placement (not scheduled)":course.year_semester]
                .push_back({&course,course.first_term_credits});
            if(!course.second_year_semester.empty())
                byTerm[course.second_year_semester].push_back({&course,course.second_term_credits});
        }
        for(const auto&[term,items]:byTerm){
            int credits=0;
            for(const auto&[item,termCredits]:items)credits+=termCredits;
            std::cout<<"\n"<<term<<" — "<<credits<<" credits\n";
            for(const auto&[item,termCredits]:items){
                std::cout<<"  "<<item->course.code<<(item->course.title.empty()?"":" — "+item->course.title)
                         <<" ("<<termCredits;
                if(item->second_term_credits>0)std::cout<<" of "<<item->course.credits;
                std::cout<<" cr)"<<(item->warning?" ⚠️":"")<<"\n";
                for(const auto&warning:item->warnings)
                    printWarning("  "+warning);
            }
        }
        return 0;
    }
    std::string output,error;
    if(!exportExcel(plan,(resources/"excel_writer.py").string(),findPython(resources),
                    output,error)){writeErrorLog("OUTPUT-1",error,"Check whether the template and Python/openpyxl are available.",error);std::cout<<error<<"\n";return 32;}
    printSuccess("Study plan saved to:");
    std::cout<<"  "<<output<<"\n";
    return 0;
}
