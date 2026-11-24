#include "page_parser.h"
#include "planner.h"
#include "config.h"
#include "acquisition.h"
#include "database_builder.h"
#include "course_update_scan.h"
#include "electives.h"
#include "json.h"
#include "runtime_paths.h"
#include "ui.h"
#include <cassert>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>

int main(){
    Config loadedConfig;
    std::string configError;
    assert(loadConfig("config.json",loadedConfig,configError));
    assert(loadedConfig.min_request_interval_ms==60000);
    assert(loadedConfig.block_cooldown_hours==24);
    assert(loadedConfig.default_max_credits==24);
    assert(loadedConfig.advanced_standing_max_credits==21);
    assert(userDataDirectoryForPlatform(RuntimePlatform::WINDOWS,
        "/home/student","/local/appdata","").generic_string()=="/local/appdata/CityUStudyPlan");
    assert(userDataDirectoryForPlatform(RuntimePlatform::WINDOWS,
        "/home/student","","").generic_string()=="/home/student/AppData/Local/CityUStudyPlan");
    assert(userDataDirectoryForPlatform(RuntimePlatform::MACOS,
        "/Users/student","","").generic_string()==
        "/Users/student/Library/Application Support/CityUStudyPlan");
    assert(userDataDirectoryForPlatform(RuntimePlatform::OTHER,
        "/home/student","","/xdg/data").generic_string()=="/xdg/data/cityu-study-plan");
    assert(userDataDirectoryForPlatform(RuntimePlatform::OTHER,
        "/home/student","","").generic_string()=="/home/student/.local/share/cityu-study-plan");
    std::unordered_map<std::string,Course> suppliedStaticDb;
    std::string suppliedDbError;
    assert(loadCoursesDb("courses_db.json",suppliedStaticDb,suppliedDbError));
    assert(suppliedStaticDb.at("LC0200A").credits_known);
    assert(suppliedStaticDb.at("LC0200B").credits==3);
    assert(suppliedStaticDb.at("CHIN1001").credits==3);
    assert(suppliedStaticDb.at("GE1601").credits==1);
    assert(suppliedStaticDb.at("GE1601").semesters.size()==1);
    assert(suppliedStaticDb.at("GE1601").semesters.front().find("Semester A")==0);
    assert(suppliedStaticDb.at("GE1601").prerequisites.empty());
    assert(suppliedStaticDb.at("GE1601").assessment.other_percent==100);
    assert(suppliedStaticDb.at("GE1601").assessment.unknown);
    assert(suppliedStaticDb.at("GE1601").catalogue_notes.find("first year")!=
           std::string::npos);
    assert(suppliedStaticDb.at("GE1501").title==
           "Chinese Civilisation - History and Philosophy");
    assert(suppliedStaticDb.at("GE1501").credits==3);
    assert(suppliedStaticDb.at("GE1501").semesters==
           std::vector<std::string>({"Semester A","Semester B","Summer"}));
    assert(suppliedStaticDb.at("GE1501").prerequisites.empty());
    assert(suppliedStaticDb.at("GE1501").assessment.other_percent==100);
    assert(suppliedStaticDb.at("GE1501").assessment.unknown);
    Course readyCandidate;
    readyCandidate.prereq_status=PrereqStatus::SATISFIED;
    readyCandidate.area="engineering";
    readyCandidate.assessment.unknown=false;
    readyCandidate.assessment.exam_percent=0;
    readyCandidate.assessment.other_percent=100;
    readyCandidate.assessment.raw_text="group project presentation";
    Course unreadyCandidate=readyCandidate;
    unreadyCandidate.prereq_status=PrereqStatus::UNSCHEDULED;
    const std::vector<std::vector<std::string>> strongestDislikes={
        {"engineering"},
        {"100% continuous assessment","group project","presentation"}
    };
    assert(candidateScore(readyCandidate,strongestDislikes)>
           candidateScore(unreadyCandidate,{}));
    assert(candidateScore(readyCandidate,{})>
           candidateScore(readyCandidate,strongestDislikes));
    // D: grouped topic labels must match either topic in catalogue metadata.
    Course philosophyCandidate;
    philosophyCandidate.prereq_status=PrereqStatus::SATISFIED;
    philosophyCandidate.tags={"philosophy"};
    assert(candidateScore(philosophyCandidate,{{"religion/philosophy"}})==
           candidateScore(philosophyCandidate,{})-100);
    philosophyCandidate.area="religion";
    // Matching both alternatives still applies only one topic penalty.
    assert(candidateScore(philosophyCandidate,{{"religion/philosophy"}})==
           candidateScore(philosophyCandidate,{})-100);
    philosophyCandidate.area="history";
    philosophyCandidate.tags={"culture"};
    assert(candidateScore(philosophyCandidate,{{"religion/philosophy"}})==
           candidateScore(philosophyCandidate,{}));
    assert(candidateScore(philosophyCandidate,{{"none / all fine"}})==
           candidateScore(philosophyCandidate,{}));

    const std::vector<std::vector<std::string>> assessmentDislikes={
        {}, {"100% continuous assessment","group project","presentation"}
    };
    Course unknownAssessment=readyCandidate;
    unknownAssessment.assessment.unknown=true;
    assert(candidateScore(unknownAssessment,assessmentDislikes)==
           candidateScore(unknownAssessment,{}));
    Course topicOnlyAssessment=readyCandidate;
    topicOnlyAssessment.assessment.exam_percent=50;
    topicOnlyAssessment.assessment.other_percent=50;
    topicOnlyAssessment.assessment.raw_text.clear();
    topicOnlyAssessment.tags={"group project","presentation"};
    assert(candidateScore(topicOnlyAssessment,assessmentDislikes)==
           candidateScore(topicOnlyAssessment,{}));
    // Confirmed assessment components remain eligible for preference matching.
    topicOnlyAssessment.assessment.raw_items={"Group Project", "Presentation"};
    assert(candidateScore(topicOnlyAssessment,assessmentDislikes)==
           candidateScore(topicOnlyAssessment,{})-20);
    assert(candidateScore(readyCandidate,assessmentDislikes)==
           candidateScore(readyCandidate,{})-30);

    Course scannedData;
    scannedData.code="SCAN1001";
    scannedData.title="New catalogue title";
    scannedData.credits=4;
    scannedData.credits_known=true;
    scannedData.prerequisite_source="UNKNOWN";
    Course staticFallback;
    staticFallback.code="SCAN1001";
    staticFallback.title="Old catalogue title";
    staticFallback.credits=3;
    staticFallback.credits_known=true;
    staticFallback.semesters={"Semester B"};
    staticFallback.prerequisites={"CS1000"};
    staticFallback.prerequisite_source="STATIC";
    const Course mergedCourse=mergeCourseData(scannedData,staticFallback);
    assert(mergedCourse.title=="New catalogue title");
    assert(mergedCourse.credits==4);
    assert(mergedCourse.semesters==
           std::vector<std::string>{"Semester B"});
    assert(mergedCourse.prerequisites==
           std::vector<std::string>{"CS1000"});
    assert(suppliedStaticDb.at("GE1401").title=="University English");
    assert(suppliedStaticDb.at("GE1401").credits==3);
    assert(suppliedStaticDb.at("GE1401").semesters==
           std::vector<std::string>({"Semester A","Semester B","Summer"}));
    assert(suppliedStaticDb.at("GE1401").prerequisites==
           std::vector<std::string>{"UNKNOWN"});
    assert(suppliedStaticDb.at("GE1401").prerequisites_text.find("LC0200B")!=
           std::string::npos);
    assert(suppliedStaticDb.at("GE1401").assessment.other_percent==100);
    assert(suppliedStaticDb.at("GE1401").assessment.unknown);
    assert(suppliedStaticDb.at("GE2410").title=="English for Engineering");
    assert(suppliedStaticDb.at("GE2410").credits==3);
    assert(suppliedStaticDb.at("GE2410").semesters==
           std::vector<std::string>({"Semester A","Semester B","Summer"}));
    assert(suppliedStaticDb.at("GE2410").prerequisites==
           std::vector<std::string>{"UNKNOWN"});
    assert(suppliedStaticDb.at("GE2410").prerequisites_text.find("LC0200A")!=
           std::string::npos);
    assert(suppliedStaticDb.at("GE2410").assessment.other_percent==100);
    assert(suppliedStaticDb.at("GE2410").assessment.unknown);
    assert(suppliedStaticDb.at("MA2185").title=="Discrete Mathematics");
    assert(suppliedStaticDb.at("MA2185").credits==3);
    assert(suppliedStaticDb.at("MA2185").semesters.size()==1);
    assert(suppliedStaticDb.at("MA2185").semesters.front().find("Semester A")==0);
    assert(suppliedStaticDb.at("MA2185").assessment.exam_percent==70);
    assert(suppliedStaticDb.at("MA2185").assessment.other_percent==30);
    assert(suppliedStaticDb.at("MA2185").assessment.unknown);
    assert(suppliedStaticDb.at("MA2185").exclusive_courses==
           std::vector<std::string>({"MA2144","MA2509","MA2504"}));
    std::unordered_map<std::string,std::string> gatewayAreas;
    std::string gatewayAreaError;
    assert(loadGatewayEducationAreas("ge_areas.json",gatewayAreas,
                                     gatewayAreaError));
    assert(gatewayAreas.at("GE1101")=="Area 1");
    assert(gatewayAreas.at("GE1202")=="Area 2");
    assert(gatewayAreas.at("GE1301")=="Area 3");
    assert(gatewayAreas.size()==161);

    const auto mergeTestDirectory=std::filesystem::current_path()/
        (".test-bundled-course-merge-"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(mergeTestDirectory);
    Course bundledDiscrete;
    bundledDiscrete.code="MA2185";
    bundledDiscrete.title="Discrete Mathematics";
    bundledDiscrete.credits=3;
    bundledDiscrete.credits_known=true;
    bundledDiscrete.semesters={"Semester A"};
    bundledDiscrete.catalogue_year="2026/27";
    bundledDiscrete.catalogue_detail_available=true;
    bundledDiscrete.exclusive_courses={"MA2144","MA2509","MA2504"};
    bundledDiscrete.catalogue_notes="Course duration: One semester.";
    Course bundledWholePerson;
    bundledWholePerson.code="GE1601";
    bundledWholePerson.title="Whole-Person Development";
    bundledWholePerson.credits=1;
    bundledWholePerson.credits_known=true;
    bundledWholePerson.semesters={"Semester A"};
    bundledWholePerson.catalogue_year="2026/27";
    bundledWholePerson.catalogue_detail_available=true;
    Course bundledUniversityEnglish=suppliedStaticDb.at("GE1401");
    Course bundledEngineeringEnglish=suppliedStaticDb.at("GE2410");
    Course bundledChineseCivilisation=suppliedStaticDb.at("GE1501");
    Course bundledNewer=bundledWholePerson;
    bundledNewer.code="LOCAL2020";
    bundledNewer.catalogue_year="2025/26";
    std::unordered_map<std::string,Course> bundledUpdate={
        {"MA2185",bundledDiscrete},
        {"GE1401",bundledUniversityEnglish},
        {"GE2410",bundledEngineeringEnglish},
        {"GE1501",bundledChineseCivilisation},
        {"GE1601",bundledWholePerson},
        {"LOCAL2020",bundledNewer}};
    Course staleDiscrete=bundledDiscrete;
    staleDiscrete.title="and CS2117";
    staleDiscrete.credits_known=false;
    staleDiscrete.semesters.clear();
    staleDiscrete.catalogue_year.clear();
    staleDiscrete.catalogue_detail_available=false;
    Course newerLocal=bundledWholePerson;
    newerLocal.code="LOCAL2020";
    newerLocal.title="Newer local record";
    newerLocal.catalogue_year="2027/28";
    Course staleUniversityEnglish=bundledUniversityEnglish;
    staleUniversityEnglish.credits_known=false;
    staleUniversityEnglish.semesters.clear();
    staleUniversityEnglish.catalogue_year.clear();
    staleUniversityEnglish.catalogue_detail_available=false;
    Course staleEngineeringEnglish=bundledEngineeringEnglish;
    staleEngineeringEnglish.credits_known=false;
    staleEngineeringEnglish.semesters.clear();
    staleEngineeringEnglish.catalogue_year.clear();
    staleEngineeringEnglish.catalogue_detail_available=false;
    Course staleChineseCivilisation=bundledChineseCivilisation;
    staleChineseCivilisation.prerequisites={"UNKNOWN"};
    staleChineseCivilisation.prerequisite_source="UNKNOWN";
    Course localOnlyCourse=bundledWholePerson;
    localOnlyCourse.code="LOCALONLY";
    std::unordered_map<std::string,Course> personalUpdate={
        {"MA2185",staleDiscrete},{"GE1401",staleUniversityEnglish},
        {"GE2410",staleEngineeringEnglish},
        {"GE1501",staleChineseCivilisation},
        {"LOCAL2020",newerLocal},
        {"LOCALONLY",localOnlyCourse}};
    const auto bundledUpdatePath=(mergeTestDirectory/"bundled.json").string();
    const auto personalUpdatePath=(mergeTestDirectory/"personal.json").string();
    std::string mergeError;
    assert(saveCoursesDb(bundledUpdatePath,bundledUpdate,mergeError));
    assert(saveCoursesDb(personalUpdatePath,personalUpdate,mergeError));
    assert(mergeBundledCourseDb(bundledUpdatePath,personalUpdatePath,mergeError));
    std::unordered_map<std::string,Course> mergedUpdate;
    assert(loadCoursesDb(personalUpdatePath,mergedUpdate,mergeError));
    assert(mergedUpdate.at("MA2185").title=="Discrete Mathematics");
    assert(mergedUpdate.at("MA2185").credits_known);
    assert(mergedUpdate.at("MA2185").exclusive_courses==
           bundledDiscrete.exclusive_courses);
    assert(mergedUpdate.at("MA2185").catalogue_notes==
           bundledDiscrete.catalogue_notes);
    assert(mergedUpdate.at("GE1401").credits==3);
    assert(mergedUpdate.at("GE1401").semesters==
           std::vector<std::string>({"Semester A","Semester B","Summer"}));
    assert(mergedUpdate.at("GE1401").prerequisites_text==
           bundledUniversityEnglish.prerequisites_text);
    assert(mergedUpdate.at("GE2410").credits==3);
    assert(mergedUpdate.at("GE2410").semesters==
           std::vector<std::string>({"Semester A","Semester B","Summer"}));
    assert(mergedUpdate.at("GE2410").prerequisites_text==
           bundledEngineeringEnglish.prerequisites_text);
    assert(mergedUpdate.at("GE1501").credits==3);
    assert(mergedUpdate.at("GE1501").semesters==
           std::vector<std::string>({"Semester A","Semester B","Summer"}));
    assert(mergedUpdate.at("GE1601").credits==1);
    assert(mergedUpdate.at("LOCAL2020").title=="Newer local record");
    assert(mergedUpdate.count("LOCALONLY")==1);
    std::filesystem::remove_all(mergeTestDirectory);

    DegreeRequirements r=parseDegreeWorks(
        "In Progress: CS2311\n"
        "Still Needed:\n"
        "CS3402\n"
        "6 credits in CS4191 or CS4192 or CS4394\n");
    assert(r.in_progress.size()==1);
    assert(r.in_progress[0]=="CS2311");
    assert(r.required_courses.size()==1);
    assert(r.required_courses[0]=="CS3402");
    assert(r.choice_groups.size()==1);
    assert(r.choice_groups[0].required_credits==6);
    assert(r.choice_groups[0].choose==0);
    assert(r.choice_groups[0].candidates.size()==3);
    const auto consecutiveRequirements=parseDegreeWorks(
        "Still Needed:\nCS1001\nMA1001\nLegend\n");
    assert(consecutiveRequirements.required_courses==
           std::vector<std::string>({"CS1001","MA1001"}));
    std::string manyChoiceText="Still Needed:\n3 credits in ";
    for(int number=1;number<=21;++number){
        if(number>1)manyChoiceText+=" or ";
        manyChoiceText+="ZZ"+std::to_string(9000+number);
    }
    const auto manyChoiceGroup=parseDegreeWorks(manyChoiceText);
    assert(manyChoiceGroup.choice_groups.size()==1);
    assert(manyChoiceGroup.choice_groups[0].candidates.size()==21);
    auto courseCountGroup=parseDegreeWorks("Choose 2 courses of CS4191 or CS4192.\n");
    assert(courseCountGroup.choice_groups.size()==1);
    assert(courseCountGroup.choice_groups[0].choose==2);
    assert(courseCountGroup.required_courses.empty());
    const auto degreeWorksSample=parseDegreeWorks(
        "Demo Student\n"
        "Name\n"
        "00000000\n"
        "Student ID\n"
        "BSc Sample\n"
        "Programme\n"
        "College Sample CollegeLevel Bachelor's DegreeDegree BSc1 - Bachelor of Science"
        "Major SampleCohort 2026Department Computer ScienceMax Credits Permitted 114"
        "Expected Graduation Date 01-Jun-2029\n"
        "Audit Date 21/11/2026\n"
        "Sample Major\n"
        "Still needed:\n"
        "ZZ1004\n"
        "Still needed: 3 Credits in CS 4191 or 4192 or 4394\n"
        "Still needed: 6 Credit Units from Area 1 or 2 or 3\n"
        "Area 1: Minimum 3 Credit Units\n"
        "Area 2: Minimum 3 Credit Units\n"
        "Area 3: Minimum 3 Credit Units\n"
        "In-progress\n"
        "Course\tTitle\tGrade\tCredits\tTerm\n"
        "ZZ1001\tCurrent Sample Course\tIP\t3\tSem A 2026/27\n"
        "Legend\n");
    assert(degreeWorksSample.student_name=="Demo Student");
    assert(degreeWorksSample.student_id=="00000000");
    assert(degreeWorksSample.programme=="BSc Sample");
    assert(degreeWorksSample.college=="Sample College");
    assert(degreeWorksSample.degree=="BSc1 - Bachelor of Science");
    assert(degreeWorksSample.major=="Sample");
    assert(degreeWorksSample.cohort=="2026");
    assert(degreeWorksSample.department=="Computer Science");
    assert(degreeWorksSample.max_credits_permitted==114);
    assert(degreeWorksSample.expected_graduation_date=="01-Jun-2029");
    assert(degreeWorksSample.audit_date=="21/11/2026");
    assert(degreeWorksSample.remaining_years==3);
    assert(degreeWorksSample.current_semester=="Semester A");
    assert(degreeWorksSample.in_progress_details.size()==1);
    assert(degreeWorksSample.in_progress_details[0].code=="ZZ1001");
    assert(degreeWorksSample.in_progress_details[0].title=="Current Sample Course");
    assert(degreeWorksSample.in_progress_details[0].credits==3);
    assert(degreeWorksSample.in_progress_details[0].term=="Semester A");
    const auto completedAudit=parseDegreeWorks(
        "Completed Courses:\n"
        "ZZ1002 Completed Sample Course\n"
        "In-progress\n"
        "Course\tTitle\tGrade\tCredits\tTerm\n"
        "ZZ1003\tCurrent Course\tIP\t3\tSemester A 2026/27\n"
        "Still Needed:\n"
        "ZZ1004\n");
    assert(completedAudit.completed_courses==std::vector<std::string>{"ZZ1002"});
    assert(completedAudit.in_progress==std::vector<std::string>{"ZZ1003"});
    assert(degreeWorksSample.required_courses==std::vector<std::string>{"ZZ1004"});
    assert(degreeWorksSample.major_courses==std::vector<std::string>{"ZZ1004"});
    assert(degreeWorksSample.choice_groups.size()==2);
    assert(degreeWorksSample.choice_groups[0].candidates==
           std::vector<std::string>({"CS4191","CS4192","CS4394"}));
    assert(degreeWorksSample.choice_groups[1].areas==
           std::vector<int>({1,2,3}));
    assert(degreeWorksSample.choice_groups[1].minimum_per_area_credits==3);
    const auto alternateMajorAudit=parseDegreeWorks(
        "105 Credit Units needed\n"
        "College Requirements\n"
        "6 Credit Units from Foreign language component or Computational language component Not complete\n"
        "Still needed:\n"
        "French Language Not complete\n"
        "6 Credits in LC 2401 or 2412 or 2406\n"
        "German Language Not complete\n"
        "6 Credits in LC 2501 or 2512 or 2506\n"
        "Japanese Language Not complete\n"
        "LC 2930\tJapanese 1\tNA\t(3)\tSem A 2026/27\n"
        "Still needed: 3 Credits in LC 2931\n"
        "Korean Language Not complete\n"
        "6 Credits in LC 2973 or 2974\n"
        "Spanish Language Not complete\n"
        "6 Credits in LC 2601 or 2612 or 2606\n"
        "Computational Language Not complete\n"
        "6 Credits in PIA 2601 or 2602\n"
        "Stream Information Not Available Not complete\n"
        "Either PIA 3800 or CAI 4001 can be chosen to fulfill stream electives requirements\n"
        "In-progress\n"
        "Course\tTitle\tGrade\tCredits\tTerm\n"
        "LC 2930\tJapanese 1\tNA\t(3)\tSem A 2026/27\n"
        "Legend\n");
    assert(alternateMajorAudit.audit_remaining_credits==105);
    assert(alternateMajorAudit.in_progress==std::vector<std::string>{"LC2930"});
    assert(alternateMajorAudit.in_progress_details[0].credits==3);
    assert(alternateMajorAudit.choice_groups.size()==2);
    const auto japaneseRequirement=std::find_if(
        alternateMajorAudit.choice_groups.begin(),
        alternateMajorAudit.choice_groups.end(),[](const ChoiceGroup&group){
            return group.candidates==std::vector<std::string>{"LC2931"};
        });
    assert(japaneseRequirement!=alternateMajorAudit.choice_groups.end());
    const auto streamRequirement=std::find_if(
        alternateMajorAudit.choice_groups.begin(),
        alternateMajorAudit.choice_groups.end(),[](const ChoiceGroup&group){
            return group.candidates==
                std::vector<std::string>({"PIA3800","CAI4001"});
        });
    assert(streamRequirement!=alternateMajorAudit.choice_groups.end());
    assert(streamRequirement->choose==1);

    const auto alternateLanguageChoices=parseDegreeWorks(
        "Still needed:\n"
        "6 credits in LC 2401 or 2412 or 2406\n"
        "Still needed:\n"
        "6 credits in LC 2501 or 2512 or 2506\n");
    assert(alternateLanguageChoices.choice_groups.size()==1);
    assert(alternateLanguageChoices.choice_groups[0].candidate_sets.size()==2);
    std::unordered_map<std::string,Course> languageCourses;
    for(const auto&code:alternateLanguageChoices.choice_groups[0].candidates) {
        Course course;
        course.code=code;
        course.credits=3;
        course.credits_known=true;
        course.semesters={"Semester A","Semester B"};
        languageCourses.emplace(code,course);
    }
    assert(validateChoiceSelection(alternateLanguageChoices.choice_groups[0],
        {"LC2401","LC2412"},languageCourses).empty());
    assert(validateChoiceSelection(alternateLanguageChoices.choice_groups[0],
        {"LC2401","LC2501"},languageCourses).find("same language option")!=
           std::string::npos);

    DegreeRequirements balancedAudit;
    balancedAudit.remaining_years=2;
    balancedAudit.audit_remaining_credits=36;
    balancedAudit.in_progress={"ZZ1001"};
    InProgressCourse currentProgressDetail;
    currentProgressDetail.code="ZZ1001";
    currentProgressDetail.credits=3;
    currentProgressDetail.credits_known=true;
    currentProgressDetail.term="Semester A";
    balancedAudit.in_progress_details={currentProgressDetail};
    for(int number=2;number<=7;++number)
        balancedAudit.required_courses.push_back(
            "ZZ"+std::to_string(1000+number));
    std::unordered_map<std::string,Course> balancedCourseRecords;
    Course currentDetails;
    currentDetails.code="ZZ1001";
    currentDetails.credits=3;
    currentDetails.credits_known=true;
    currentDetails.semesters={"Semester A","Semester B"};
    balancedCourseRecords.emplace(currentDetails.code,currentDetails);
    for(const auto&code:balancedAudit.required_courses) {
        Course course;
        course.code=code;
        course.credits=3;
        course.credits_known=true;
        course.semesters={"Semester A","Semester B"};
        balancedCourseRecords.emplace(code,course);
    }
    const Plan balancedSchedule=makePlan(
        balancedAudit,balancedCourseRecords,{},loadedConfig,false);
    assert(balancedSchedule.audit_remaining_credits==36);
    assert(balancedSchedule.in_progress_credits==3);
    assert(balancedSchedule.remaining_credits==33);
    assert(balancedSchedule.average_credits_per_term==11);
    assert(balancedSchedule.unplanned_audit_credits==15);
    for(const auto&course:balancedSchedule.courses)
        if(course.requirement!="In progress")
            assert(course.year_semester!="Year 1 Semester A");

    const auto conflictingTerms=parseDegreeWorks(
        "In-progress\n"
        "Course\tTitle\tGrade\tCredits\tTerm\n"
        "ZZ1001\tCurrent course A\tIP\t3\tSem A 2026/27\n"
        "ZZ1002\tCurrent course B\tIP\t3\tSem B 2026/27\n"
        "Legend\n");
    assert(conflictingTerms.conflicting_in_progress_terms);
    assert(conflictingTerms.current_semester.empty());

    Assessment continuousAssessment;
    continuousAssessment.exam_percent=0;
    continuousAssessment.other_percent=100;
    continuousAssessment.unknown=false;
    assert(is100PercentContinuousAssessment(continuousAssessment));
    continuousAssessment.unknown=true;
    assert(!is100PercentContinuousAssessment(continuousAssessment));
    continuousAssessment.unknown=false;
    continuousAssessment.exam_percent=20;
    continuousAssessment.other_percent=80;
    assert(!is100PercentContinuousAssessment(continuousAssessment));

    ChoiceGroup creditChoice;
    creditChoice.required_credits=6;
    std::unordered_map<std::string,Course> electiveCourses;
    for(const std::string&code:{"EL1001","EL1002"}){
        Course elective;
        elective.code=code;
        elective.title="Verified elective";
        elective.credits=3;
        elective.credits_known=true;
        elective.semesters={"Semester A"};
        electiveCourses[code]=elective;
        creditChoice.candidates.push_back(code);
    }
    assert(canSatisfyChoiceGroup(creditChoice,creditChoice.candidates,electiveCourses));
    ChoiceGroup areaChoice;
    areaChoice.required_credits=6;
    areaChoice.areas={1,2,3};
    areaChoice.minimum_per_area_credits=3;
    auto areaCourses=electiveCourses;
    areaCourses["EL1001"].area="Area 1";
    areaCourses["EL1002"].area="Area 2";
    areaChoice.candidates={"EL1001","EL1002"};
    assert(canSatisfyChoiceGroup(areaChoice,areaChoice.candidates,areaCourses));
    areaCourses["EL1002"].area="Area 1";
    assert(!canSatisfyChoiceGroup(areaChoice,areaChoice.candidates,areaCourses));
    areaCourses["EL1002"].area="Area 2";
    assert(validateChoiceSelection(areaChoice,{"EL1001","EL1002"},
                                   areaCourses).empty());
    areaCourses["EL1002"].area="Area 1";
    assert(validateChoiceSelection(areaChoice,{"EL1001","EL1002"},
                                   areaCourses).find("different listed areas")!=
           std::string::npos);
    ChoiceGroup sixCreditChoice;
    sixCreditChoice.required_credits=6;
    assert(validateChoiceSelection(sixCreditChoice,{"EL1001","EL1002"},
                                   electiveCourses).empty());
    assert(!validateChoiceSelection(sixCreditChoice,{"EL1001","EL1001"},
                                    electiveCourses).empty());
    Course notOfferedCourse;
    notOfferedCourse.semesters={"Not offered"};
    assert(hasSchedulableOffering(notOfferedCourse));
    assert(hasOfferingInTerm(notOfferedCourse,"Year 1 Semester A"));
    assert(hasOfferingInTerm(notOfferedCourse,"Year 1 Semester B"));
    assert(!hasOfferingInTerm(notOfferedCourse,"Year 1 Summer"));
    Course xMarkedNotOffered;
    xMarkedNotOffered.semesters={"X"};
    assert(hasSchedulableOffering(xMarkedNotOffered));
    assert(hasOfferingInTerm(xMarkedNotOffered,"Year 1 Semester A"));
    assert(hasOfferingInTerm(xMarkedNotOffered,"Year 1 Semester B"));
    assert(!hasOfferingInTerm(xMarkedNotOffered,"Year 1 Summer"));
    assert(!canSatisfyChoiceGroup(creditChoice,{"EL1001"},electiveCourses));
    creditChoice.choose=1;
    assert(!canSatisfyChoiceGroup(creditChoice,creditChoice.candidates,electiveCourses));
    creditChoice.choose=2;
    assert(canSatisfyChoiceGroup(creditChoice,creditChoice.candidates,electiveCourses));
    creditChoice.choose=0;
    electiveCourses["EL1002"].credits_known=false;
    assert(canSatisfyChoiceGroup(creditChoice,creditChoice.candidates,electiveCourses));
    electiveCourses["EL1002"].credits_known=true;
    electiveCourses["EL1002"].semesters={"Not offered"};
    assert(canSatisfyChoiceGroup(creditChoice,creditChoice.candidates,electiveCourses));

    std::unordered_map<std::string,Course> courses;
    Course c;c.code="CS3402";c.prerequisite_source="STATIC";c.prerequisites={"CS2311"};
    courses["CS3402"]=c;
    classifyPrerequisites(r,courses);
    assert(courses["CS3402"].prereq_status==PrereqStatus::SCHEDULED);
    DegreeRequirements completedPrerequisite;
    completedPrerequisite.completed_courses={"CS2311"};
    courses["CS3402"].prereq_status=PrereqStatus::UNKNOWN;
    classifyPrerequisites(completedPrerequisite,courses);
    assert(courses["CS3402"].prereq_status==PrereqStatus::SATISFIED);

    Config config;
    Acquisition acquisition(config,AcquisitionMode::FULL_DYNAMIC);
    assert(acquisition.pendingNetworkFetches({"LC0200A"},true)==1);
    assert(acquisition.pendingNetworkFetchCodes({"LC0200A"},true)==
           std::vector<std::string>{"LC0200A"});
    Acquisition localOnly(config,AcquisitionMode::STATIC_FIRST);
    std::string localDatabaseError;
    assert(localOnly.loadStaticDb("../tests/fixtures/legacy_courses.json",
                                  localDatabaseError));
    const auto unavailable=localOnly.unavailableWithoutNetwork(
        {"CS3402","LC0200A","LC0200B","MATH1000"},true);
    assert(unavailable.size()==4);
    assert(unavailable[0]=="CS3402");
    assert(unavailable[1]=="LC0200A");
    assert(unavailable[2]=="LC0200B");
    assert(unavailable[3]=="MATH1000");

    const auto originalDirectory=std::filesystem::current_path();
    const auto blockedTestDirectory=originalDirectory/
        (".test-blocked-acquisition-"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(blockedTestDirectory/".cityu_cache");
    Course partialLocal;
    partialLocal.code="STUDENT1000";
    partialLocal.title="Locally known title";
    partialLocal.credits=3;
    partialLocal.credits_known=true;
    partialLocal.semesters={"Semester A"};
    partialLocal.prerequisite_source="UNKNOWN";
    partialLocal.assessment.unknown=true;
    Course completeLocal=partialLocal;
    completeLocal.code="LOCAL2000";
    completeLocal.title="Verified local course";
    completeLocal.catalogue_detail_available=true;
    std::unordered_map<std::string,Course> partialDatabase={
        {partialLocal.code,partialLocal},{completeLocal.code,completeLocal}};
    std::string partialSaveError;
    if(!saveCoursesDb((blockedTestDirectory/"courses.json").string(),
                      partialDatabase,partialSaveError)){
        std::cerr<<partialSaveError<<"\n";
        return 1;
    }
    {
        std::ofstream cooldown(blockedTestDirectory/".cityu_cache"/
                               "catalogue-cooldown-until");
        cooldown<<static_cast<long long>(std::time(nullptr))+3600<<"\n";
    }
    std::filesystem::current_path(blockedTestDirectory);
    Acquisition blockedLocal(config,AcquisitionMode::STATIC_FIRST);
    std::string blockedDatabaseError;
    assert(blockedLocal.loadStaticDb("courses.json",blockedDatabaseError));
    assert(blockedLocal.pendingNetworkFetches(
        {"STUDENT1000","LOCAL2000"},false)==1);
    const auto verifiedResult=blockedLocal.fetch("LOCAL2000",false);
    assert(!verifiedResult.needs_fix);
    assert(verifiedResult.course.title=="Verified local course");
    const auto blockedResult=blockedLocal.fetch("STUDENT1000",false);
    assert(blockedResult.needs_fix);
    assert(blockedResult.site_blocked);
    assert(blockedResult.course.title=="Locally known title");
    assert(blockedResult.course.credits_known);
    Course scannedPartial=partialLocal;
    scannedPartial.title="New scanned title";
    std::unordered_map<std::string,Course> scannedDatabase={
        {scannedPartial.code,scannedPartial}};
    std::string scannedSaveError;
    assert(saveCoursesDb((blockedTestDirectory/"courses_scanned.json").string(),
                         scannedDatabase,scannedSaveError));
    Acquisition layered(config,AcquisitionMode::STATIC_FIRST);
    std::string layeredError;
    assert(layered.loadStaticDb("courses_scanned.json",layeredError));
    assert(layered.loadFallbackDb("courses.json",layeredError));
    assert(layered.pendingNetworkFetches(
        {"STUDENT1000","LOCAL2000"},false)==1);
    const auto fallbackOnly=layered.fetch("LOCAL2000",false);
    assert(!fallbackOnly.needs_fix);
    assert(fallbackOnly.course.title=="Verified local course");
    const auto layeredBlocked=layered.fetch("STUDENT1000",false);
    assert(layeredBlocked.needs_fix);
    assert(layeredBlocked.course.title=="New scanned title");
    assert(layeredBlocked.course.credits_known);
    assert(layeredBlocked.course.semesters==
           std::vector<std::string>{"Semester A"});
    assert(blockedResult.course.semesters==std::vector<std::string>{"Semester A"});
    std::filesystem::current_path(originalDirectory);
    std::filesystem::remove_all(blockedTestDirectory);

    std::unordered_map<std::string,Course> legacy;
    std::string legacyError;
    assert(loadCoursesDb("../tests/fixtures/legacy_courses.json",legacy,legacyError));
    assert(legacy.at("CS3402").prerequisites.size()==1);
    assert(legacy.at("CS3402").prerequisites[0]=="CS2311");
    assert(legacy.at("CS3402").credits_known);
    assert(legacy.at("CS3402").assessment.unknown);
    assert(legacy.at("CS3402").tags.size()==2);
    assert(legacy.at("CS5000").assessment.unknown);
    assert(legacy.at("CS5001").assessment.exam_percent==0);
    assert(legacy.at("CS5001").assessment.other_percent==100);
    assert(!legacy.at("CS5001").assessment.unknown);
    assert(legacy.at("CS5002").assessment.exam_percent==30);
    assert(legacy.at("CS5002").assessment.other_percent==70);
    assert(!legacy.at("CS5002").assessment.unknown);
    assert(legacy.at("CS5003").assessment.unknown);
    legacy.at("CS5002").prerequisites_text="CS2311";
    legacy.at("CS5002").exclusive_courses={"CS5004","CS5005"};
    legacy.at("CS5002").catalogue_notes="Course duration: One semester.";
    legacy.at("CS5002").catalogue_year="2026/27";
    legacy.at("CS5002").catalogue_detail_available=true;
    legacy.at("CS5003").catalogue_year="2026/27";
    legacy.at("CS5003").catalogue_detail_available=true;
    const std::string roundTripPath=".test-courses-roundtrip.json";
    std::error_code fileError;
    std::filesystem::remove(roundTripPath,fileError);
    std::string saveError;
    assert(saveCoursesDb(roundTripPath,legacy,saveError));
    std::unordered_map<std::string,Course> roundTripped;
    std::string roundTripError;
    assert(loadCoursesDb(roundTripPath,roundTripped,roundTripError));
    assert(roundTripped.at("CS5002").title=="Split assessment");
    assert(roundTripped.at("CS5002").assessment.other_percent==70);
    assert(roundTripped.at("CS5002").prerequisites_text=="CS2311");
    assert(roundTripped.at("CS5002").exclusive_courses==
           std::vector<std::string>({"CS5004","CS5005"}));
    assert(roundTripped.at("CS5002").catalogue_notes=="Course duration: One semester.");
    assert(roundTripped.at("CS5002").catalogue_year=="2026/27");
    assert(roundTripped.at("CS5002").catalogue_detail_available);
    Acquisition pastedLocal(config,AcquisitionMode::STATIC_FIRST);
    std::string pastedDatabaseError;
    assert(pastedLocal.loadStaticDb(roundTripPath,pastedDatabaseError));
    assert(pastedLocal.pendingNetworkFetches({"CS5003"},false)==0);
    std::filesystem::remove(roundTripPath,fileError);
    DegreeRequirements noRequirements;
    classifyPrerequisites(noRequirements,legacy);
    assert(legacy.at("CS3402").prereq_status==PrereqStatus::UNSCHEDULED);
    assert(legacy.at("LC0200A").prereq_status==PrereqStatus::UNKNOWN);

    DegreeRequirements scheduleReq;
    scheduleReq.in_progress={"DEMO1001"};
    scheduleReq.required_courses={"DEMO1001","DEMO1002"};
    std::unordered_map<std::string,Course> scheduleCourses;
    Course inProgress;
    inProgress.code="DEMO1001";
    inProgress.credits_known=true;
    inProgress.semesters={"Semester A","Semester B"};
    inProgress.prerequisite_source="STATIC";
    scheduleCourses[inProgress.code]=inProgress;
    Course dependent;
    dependent.code="DEMO1002";
    dependent.credits_known=true;
    dependent.semesters={"Semester A","Semester B"};
    dependent.prerequisite_source="STATIC";
    dependent.prerequisites={"DEMO1001"};
    scheduleCourses[dependent.code]=dependent;
    auto staleChosen=scheduleCourses;
    classifyPrerequisites(scheduleReq,scheduleCourses);
    Config scheduleConfig;
    auto schedule=makePlan(scheduleReq,scheduleCourses,staleChosen,scheduleConfig,false);
    assert(schedule.courses[0].course.code=="DEMO1001");
    assert(schedule.courses[1].course.code=="DEMO1002");
    assert(!schedule.courses[1].warning);
    auto advancedSchedule=makePlan(scheduleReq,scheduleCourses,staleChosen,scheduleConfig,true);
    assert(advancedSchedule.courses[0].year_semester=="Year 1 Semester A");
    assert(advancedSchedule.plan_years==3);
    assert(advancedSchedule.courses.size()==2);

    DegreeRequirements englishReq;
    englishReq.in_progress={"LC0200A"};
    englishReq.required_courses={"LC0200B"};
    englishReq.current_semester="Semester A";
    englishReq.remaining_years=3;
    InProgressCourse englishInProgress;
    englishInProgress.code="LC0200A";
    englishInProgress.term="Semester A";
    englishReq.in_progress_details={englishInProgress};
    Course englishA;englishA.code="LC0200A";englishA.credits=3;
    englishA.credits_known=true;englishA.semesters={"Semester A","Semester B"};
    Course englishB=englishA;englishB.code="LC0200B";
    std::unordered_map<std::string,Course> englishCourses={
        {"LC0200A",englishA},{"LC0200B",englishB}};
    auto englishPlan=makePlan(englishReq,englishCourses,englishCourses,
                              scheduleConfig,false);
    assert(englishPlan.issues.empty());
    assert(englishPlan.courses.size()==2);
    assert(englishPlan.courses[0].year_semester=="Year 1 Semester A");
    assert(englishPlan.courses[1].year_semester=="Year 1 Semester B");
    assert(englishPlan.plan_years==3);

    DegreeRequirements summerReq;
    summerReq.current_semester="Semester B";
    summerReq.remaining_years=2;
    summerReq.required_courses={"DEMO1003"};
    Course summerCourse;
    summerCourse.code="DEMO1003";summerCourse.credits=3;
    summerCourse.credits_known=true;summerCourse.semesters={"Summer"};
    summerCourse.prerequisite_source="STATIC";
    std::unordered_map<std::string,Course> summerCourses={{summerCourse.code,summerCourse}};
    auto summerPlan=makePlan(summerReq,summerCourses,summerCourses,
                             scheduleConfig,false);
    assert(summerPlan.courses.size()==1);
    assert(summerPlan.courses[0].manual_placement);
    assert(summerPlan.courses[0].year_semester=="Summer (manual placement)");
    assert(summerPlan.courses[0].first_term_credits==3);

    // Summer offerings must not enter automatic schedules or workload targets.
    DegreeRequirements mixedSummerReq;
    mixedSummerReq.remaining_years=2;
    mixedSummerReq.required_courses={"MIX1001","MIX1002","MIX1003"};
    std::unordered_map<std::string,Course> mixedSummerCourses;
    for(const auto&code:mixedSummerReq.required_courses){
        Course c;
        c.code=code;c.credits=3;c.credits_known=true;
        c.semesters={"Semester A","Semester B","Summer"};
        c.prerequisite_source="STATIC";
        mixedSummerCourses[code]=c;
    }
    const auto mixedSummerPlan=makePlan(mixedSummerReq,mixedSummerCourses,{},scheduleConfig,false);
    assert(mixedSummerPlan.courses.size()==3);
    assert(mixedSummerPlan.average_credits_per_term==3); // 9 credits / 3 remaining A/B terms
    for(const auto&c:mixedSummerPlan.courses){
        assert(!c.manual_placement);
        assert(c.year_semester.find("Summer")==std::string::npos);
    }
    mixedSummerCourses["MIX1003"].title="Final Year Project";
    const auto summerProjectPlan=makePlan(mixedSummerReq,mixedSummerCourses,{},scheduleConfig,false);
    for(const auto&c:summerProjectPlan.courses)
        assert(c.year_semester.find("Summer")==std::string::npos);

    DegreeRequirements endOfPlanReq;
    endOfPlanReq.remaining_years=1;
    endOfPlanReq.required_courses={"END1001","END1002"};
    Course lastPrerequisite;
    lastPrerequisite.code="END1001";lastPrerequisite.credits=3;
    lastPrerequisite.credits_known=true;lastPrerequisite.semesters={"Semester B"};
    lastPrerequisite.prerequisite_source="STATIC";
    Course endDependent=lastPrerequisite;
    endDependent.code="END1002";endDependent.prerequisites={"END1001"};
    const std::unordered_map<std::string,Course> endCourses={
        {"END1001",lastPrerequisite},{"END1002",endDependent}};
    const auto endPlan=makePlan(endOfPlanReq,endCourses,{},scheduleConfig,false);
    assert(endPlan.courses.size()==2);
    assert(!endPlan.courses[0].manual_placement);
    assert(endPlan.courses[1].manual_placement);
    assert(endPlan.courses[1].first_term_credits==3);

    DegreeRequirements placementReq;
    placementReq.remaining_years=3;
    placementReq.current_semester="Semester B";
    placementReq.conflicting_in_progress_terms=true;
    placementReq.in_progress={"RULEIP9001"};
    placementReq.required_courses={
        "RULEPROJ9001","RULEINTERN9001","RULEMISS9001","RULEUNKNOWN9001"};
    std::unordered_map<std::string,Course> placementCourses;
    Course currentCourse;
    currentCourse.code="RULEIP9001";
    currentCourse.title="Current course";
    currentCourse.credits=3;
    currentCourse.credits_known=true;
    currentCourse.semesters={"Semester B"};
    currentCourse.prerequisite_source="STATIC";
    placementCourses[currentCourse.code]=currentCourse;
    Course projectCourse=currentCourse;
    projectCourse.code="RULEPROJ9001";
    projectCourse.title="Final Year Project";
    projectCourse.semesters.clear();
    placementCourses[projectCourse.code]=projectCourse;
    Course internshipCourse=currentCourse;
    internshipCourse.code="RULEINTERN9001";
    internshipCourse.title="Professional Internship";
    internshipCourse.semesters={"Semester A","Semester B"};
    placementCourses[internshipCourse.code]=internshipCourse;
    Course missingOfferingCourse=currentCourse;
    missingOfferingCourse.code="RULEMISS9001";
    missingOfferingCourse.title="Course with missing offering terms";
    missingOfferingCourse.semesters.clear();
    placementCourses[missingOfferingCourse.code]=missingOfferingCourse;
    Course unknownCourse;
    unknownCourse.code="RULEUNKNOWN9001";
    unknownCourse.prerequisite_source="UNKNOWN";
    placementCourses[unknownCourse.code]=unknownCourse;
    const auto placementPlan=makePlan(
        placementReq,placementCourses,placementCourses,scheduleConfig,false);
    auto placementFor=[&](const std::string&code)->const PlanCourse&{
        const auto found=std::find_if(
            placementPlan.courses.begin(),placementPlan.courses.end(),
            [&](const PlanCourse&course){return course.course.code==code;});
        assert(found!=placementPlan.courses.end());
        return *found;
    };
    assert(placementFor("RULEIP9001").year_semester=="Year 1 Semester A");
    assert(placementFor("RULEPROJ9001").year_semester.rfind("Year 3 ",0)==0);
    assert(placementFor("RULEINTERN9001").manual_placement);
    assert(placementFor("RULEMISS9001").year_semester.find("Summer")==
           std::string::npos);
    assert(!placementFor("RULEMISS9001").warnings.empty());
    assert(!placementPlan.warnings.empty());
    assert(!placementFor("RULEUNKNOWN9001").course.credits_known);
    assert(placementFor("RULEUNKNOWN9001").first_term_credits==3);
    assert(hasSchedulableOffering(missingOfferingCourse));
    for(const auto&course:placementPlan.courses)
        if(!course.manual_placement&&course.course.code!="RULEIP9001")
            assert(course.year_semester.find("Summer")==std::string::npos);

    DegreeRequirements balancedReq;
    balancedReq.remaining_years=2;
    balancedReq.required_courses={"BAL1001","BAL1002","BAL1003","BAL1004"};
    std::unordered_map<std::string,Course> balancedCourses;
    for(const auto&code:balancedReq.required_courses){
        Course course;
        course.code=code;
        course.credits=3;
        course.credits_known=true;
        course.semesters={"Semester A","Semester B"};
        course.prerequisite_source="STATIC";
        balancedCourses[code]=course;
    }
    const auto balancedPlan=makePlan(
        balancedReq,balancedCourses,balancedCourses,scheduleConfig,false);
    std::unordered_map<std::string,int> balancedCredits;
    for(const auto&course:balancedPlan.courses)
        balancedCredits[course.year_semester]+=course.first_term_credits;
    int smallestTerm=1000,largestTerm=0;
    for(const auto&[term,credits]:balancedCredits){
        smallestTerm=std::min(smallestTerm,credits);
        largestTerm=std::max(largestTerm,credits);
        assert(term.find("Summer")==std::string::npos);
    }
    assert(largestTerm-smallestTerm<=3);

    DegreeRequirements illustrativeReq;
    illustrativeReq.illustrative=true;
    ChoiceGroup illustrativeChoice;
    illustrativeChoice.required_credits=3;
    illustrativeChoice.candidates={"EL1001"};
    illustrativeReq.choice_groups.push_back(illustrativeChoice);
    std::unordered_map<std::string,Course> illustrativeChosen={
        {"EL1001",electiveCourses.at("EL1001")}};
    auto illustrativePlan=makePlan(illustrativeReq,electiveCourses,
                                   illustrativeChosen,scheduleConfig,false);
    assert(illustrativePlan.illustrative);
    assert(illustrativePlan.courses[0].requirement==
           "Catalogue sample only; not a degree requirement");
    const auto emptyAuditPlan=makePlan(
        DegreeRequirements{},std::unordered_map<std::string,Course>{},{},
        scheduleConfig,false);
    assert(std::any_of(emptyAuditPlan.warnings.begin(),emptyAuditPlan.warnings.end(),
        [](const std::string&warning){
            return warning.find("verify that the report was parsed correctly")!=
                   std::string::npos;
        }));
    DegreeRequirements packingReq;
    std::unordered_map<std::string,Course> packingCourses;
    std::unordered_map<std::string,Course> packingChosen;
    for(int i=0;i<8;++i){
        const std::string code="PACK"+std::to_string(i);
        packingReq.required_courses.push_back(code);
        Course course;
        course.code=code;
        course.title=code;
        course.credits=3;
        course.credits_known=true;
        course.semesters={"Semester A","Semester B"};
        course.prerequisite_source="STATIC";
        packingCourses[code]=course;
        packingChosen[code]=course;
    }
    classifyPrerequisites(packingReq,packingCourses);
    auto packed=makePlan(packingReq,packingCourses,packingChosen,scheduleConfig,false);
    assert(packed.courses[0].year_semester=="Year 1 Semester B");
    std::unordered_map<std::string,int> packedCredits;
    for(const auto&course:packed.courses){
        packedCredits[course.year_semester]+=course.first_term_credits;
        assert(course.year_semester.find("Summer")==std::string::npos);
    }
    int packedMinimum=1000,packedMaximum=0;
    for(const auto&[term,credits]:packedCredits){
        packedMinimum=std::min(packedMinimum,credits);
        packedMaximum=std::max(packedMaximum,credits);
    }
    assert(packedMaximum-packedMinimum<=3);
    DegreeRequirements studentRequirements;
    studentRequirements.required_courses={"STUDENT1000"};
    Course studentCourse;
    studentCourse.code="STUDENT1000";
    studentCourse.credits=3;
    studentCourse.credits_known=true;
    studentCourse.semesters={"Semester A"};
    Course catalogueOnlyCourse=studentCourse;
    catalogueOnlyCourse.code="CATALOGUE1000";
    std::unordered_map<std::string,Course> catalogue={
        {studentCourse.code,studentCourse},{catalogueOnlyCourse.code,catalogueOnlyCourse}};
    const auto studentPlan=makePlan(studentRequirements,catalogue,{},scheduleConfig,false);
    assert(studentPlan.issues.empty());
    assert(studentPlan.courses.size()==1);
    assert(studentPlan.courses[0].course.code=="STUDENT1000");

    const Assessment split=parseCourseAssessment(
        "<div id='div_assessment_exam_pct'>40%</div>"
        "<div id='div_assessment_quiz_pct'>10%</div>"
        "<div id='div_assessment_continuous_assessment_pct'>30%</div>"
        "<div id='div_assessment_attendance_pct'>20%</div>");
    assert(split.exam_percent==40);
    assert(split.other_percent==60);
    assert(!split.unknown);
    assert(split.raw_items.size()==4);
    assert((parseCourseOfferingTerms(
        "<div id='div_course_offering_term'>Semester A, Semester B and Summer</div>")==
        std::vector<std::string>{"Semester A","Semester B","Summer"}));
    assert((parseCourseOfferingTerms(
        "<div id='div_course_offering_term'>Not offering</div>")==
        std::vector<std::string>{"Not offered"}));
    const Course parsedCourse=parseCoursePageHtml(
        "CS1234","https://catalogue.example/course",
        "<div id='div_course_code_and_title'>CS1234 Sample Course</div>"
        "<div id='div_course_credits'>3 credit units</div>"
        "<div id='div_course_offering_term'>Semester A and Summer</div>"
        "<div id='div_prerequisites'>CS1000</div>"
        "<table><tr><td>Exclusive Courses:</td><td>MA2144 or MA2509 or MA2504</td></tr></table>"
        "<div id='div_assessment_exam_pct'>40%</div>"
        "<div id='div_assessment_quiz_pct'>60%</div>");
    assert(parsedCourse.title=="Sample Course");
    assert(parsedCourse.credits_known&&parsedCourse.credits==3);
    assert(parsedCourse.semesters==std::vector<std::string>({"Semester A","Summer"}));
    assert(parsedCourse.prerequisites==std::vector<std::string>{"CS1000"});
    assert(parsedCourse.exclusive_courses==
           std::vector<std::string>({"MA2144","MA2509","MA2504"}));
    assert(parsedCourse.assessment.exam_percent==40);
    assert(parsedCourse.assessment.other_percent==60);
    assert(!parsedCourse.assessment.unknown);
    const Course indicativeCourse=parseCoursePageHtml(
        "CS1235","https://catalogue.example/indicative",
        "<div id='div_course_code_and_title'>CS1235 Indicative Course</div>"
        "<div id='div_course_credits'>3</div>"
        "<div id='div_course_offering_term'>Semester A</div>"
        "<div id='div_prerequisites'>None</div>"
        "<div>Assessment (Indicative only)</div>"
        "<div id='div_assessment_exam_pct'>50%</div>"
        "<div id='div_assessment_ca_pct'>50%</div>");
    assert(indicativeCourse.assessment.exam_percent==50);
    assert(indicativeCourse.assessment.other_percent==50);
    assert(indicativeCourse.assessment.unknown);
    const Course tableCourse=parseCoursePageHtml(
        "GE1107","https://www.cityu.edu.hk/catalogue/ug/current/course/GE1107.htm",
        "<table>"
        "<tr><td><strong>GE1107 - Cities in Chinese History and Culture</strong></td></tr>"
        "<tr><td><strong>Credit Units</strong></td><td>3</td></tr>"
        "<tr><td><strong>GE Area</strong></td><td>Area 1: Arts and Humanities</td></tr>"
        "<tr><td><strong>Course Offering Term*</strong></td>"
        "<td>Not offering in current academic year</td></tr>"
        "<tr><td><strong>Pre-requisite(s)</strong></td><td>None</td></tr>"
        "</table>");
    assert(tableCourse.title=="Cities in Chinese History and Culture");
    assert(tableCourse.credits_known&&tableCourse.credits==3);
    assert(tableCourse.area=="Area 1: Arts and Humanities");
    assert(tableCourse.semesters==std::vector<std::string>{"Not offered"});
    assert(tableCourse.prerequisites.empty());

    PastedCourseDatabase pasted;
    std::string pastedError;
    const std::string pastedText=
        "Undergraduate Catalogue 2026/27\n"
        "CS9000 Index title\n"
        "COURSES >>>\n"
        "CS9000 - Detailed course title\n"
        "Credit Units\n3\n"
        "Pre-requisite(s)\nCS1000/CS1001 or CS2000\n"
        "Exclusive Courses:\nMA2144 or MA2509 or MA2504\n"
        "Course Offering Term*:\nSemester A 2026/27\n"
        "Assessment (Indicative only, please check details)\n"
        "Continuous Assessment: 40%\nExamination: 60%\n"
        "Detailed Course Information\nCS9000.pdf\n";
    assert(parsePastedCourseDatabase(pastedText,pasted,pastedError));
    assert(pasted.catalogue_year=="2026/27");
    assert(pasted.courses.size()==1);
    assert(pasted.detailed_codes.count("CS9000")==1);
    assert(pasted.courses.at("CS9000").title=="Detailed course title");
    assert(pasted.courses.at("CS9000").catalogue_detail_available);
    assert(pasted.courses.at("CS9000").credits_known);
    assert(pasted.courses.at("CS9000").semesters==std::vector<std::string>{"Semester A"});
    assert(pasted.courses.at("CS9000").assessment.exam_percent==60);
    assert(pasted.courses.at("CS9000").assessment.other_percent==40);
    assert(pasted.courses.at("CS9000").assessment.unknown);
    assert(pasted.courses.at("CS9000").prerequisite_source=="UNKNOWN");
    assert(pasted.courses.at("CS9000").prerequisites_text=="CS1000/CS1001 or CS2000");
    assert(pasted.courses.at("CS9000").exclusive_courses==
           std::vector<std::string>({"MA2144","MA2509","MA2504"}));

    DegreeRequirements exclusiveReq;
    exclusiveReq.required_courses={"MA2185","MA2144"};
    Course discrete=bundledDiscrete;
    Course excludedCourse=bundledDiscrete;
    excludedCourse.code="MA2144";
    excludedCourse.title="Alternative discrete course";
    excludedCourse.exclusive_courses.clear();
    std::unordered_map<std::string,Course> exclusiveCourses={
        {"MA2185",discrete},{"MA2144",excludedCourse}};
    const auto exclusivePlan=makePlan(exclusiveReq,exclusiveCourses,{},
                                      scheduleConfig,false);
    assert(!exclusivePlan.issues.empty());
    assert(exclusivePlan.issues[0].find("exclusive courses")!=std::string::npos);

    std::istringstream uiInput("A,B\nA\nhttps://Example.org/CaseSensitive\n");
    std::ostringstream uiOutput;
    auto *oldInput=std::cin.rdbuf(uiInput.rdbuf());
    auto *oldOutput=std::cout.rdbuf(uiOutput.rdbuf());
    const auto answer=readMultiSelect("Test preference",{"topic","none / all fine"},false);
    assert(answer.selections.size()==1&&answer.selections[0]=="topic");
    assert(readPrompt("URL")=="https://Example.org/CaseSensitive");
    std::cin.rdbuf(oldInput);
    std::cout.rdbuf(oldOutput);

    // D review: invalid catalogue substitutions and area-prefix lookalikes.
    ChoiceGroup restrictedChoice;
    restrictedChoice.choose=1;
    restrictedChoice.candidates={"EL1001"};
    assert(validateChoiceSelection(restrictedChoice,{"EL1002"},electiveCourses)
        .find("not listed")!=std::string::npos);
    assert(!validateChoiceSelection(restrictedChoice,{},electiveCourses).empty());
    assert(!validateChoiceSelection(restrictedChoice,{"EL1001","EL1001"},electiveCourses).empty());
    Course misleadingArea;
    misleadingArea.area="Area 10";
    assert(!courseMatchesArea(misleadingArea,{1}));
    misleadingArea.area="Area 2";
    assert(courseMatchesArea(misleadingArea,{2}));
    Course earthTopic;
    earthTopic.tags={"earth science"};
    assert(candidateScore(earthTopic,{{"music/art"}})==candidateScore(earthTopic,{}));
    earthTopic.tags={"arts"};
    assert(candidateScore(earthTopic,{{"music/art"}})==candidateScore(earthTopic,{})-100);

    // Picker output must preserve original requirement indexes after visiting
    // small groups first; downstream workload review uses those indexes.
    DegreeRequirements pickerReq;
    ChoiceGroup larger;
    larger.choose=1;
    larger.candidates={"EL1001","EL1002"};
    larger.purpose="Larger requirement";
    ChoiceGroup smaller;
    smaller.choose=1;
    smaller.candidates={"EL1003"};
    smaller.purpose="Smaller requirement";
    pickerReq.choice_groups={larger,smaller};
    auto pickerCourses=electiveCourses;
    pickerCourses["EL1003"]=pickerCourses.at("EL1001");
    pickerCourses["EL1003"].code="EL1003";
    std::unordered_map<std::string,Course> pickerChosen;
    std::istringstream pickerInput("1\n999\n1,1\n1,\n1\n");
    std::ostringstream pickerOutput;
    oldInput=std::cin.rdbuf(pickerInput.rdbuf());
    oldOutput=std::cout.rdbuf(pickerOutput.rdbuf());
    const auto picked=selectElectives(pickerReq,pickerCourses,pickerChosen,
        {larger.candidates,smaller.candidates},{true,true},{},false);
    std::cin.rdbuf(oldInput);
    std::cout.rdbuf(oldOutput);
    assert(picked.selections[0].course_codes==std::vector<std::string>{"EL1001"});
    assert(picked.selections[1].course_codes==std::vector<std::string>{"EL1003"});
    assert(picked.ranked_groups[0]==larger.candidates);
    assert(pickerOutput.str().find("not shown above")!=std::string::npos);
    assert(pickerOutput.str().find("only once")!=std::string::npos);
    assert(pickerOutput.str().find("distinct, valid")!=std::string::npos);
    assert(pickerOutput.str().find("Assessment: Unknown / unconfirmed")!=std::string::npos);

    // A user who initially skips pagination can still request M at selection.
    DegreeRequirements pagedReq;
    ChoiceGroup pagedGroup;
    pagedGroup.choose=1;
    std::unordered_map<std::string,Course> pagedCourses;
    for (int i=0;i<21;++i) {
        Course c;
        c.code="ZZ"+std::to_string(1000+i);
        c.credits_known=true;
        c.prereq_status=i<20?PrereqStatus::SATISFIED:PrereqStatus::UNKNOWN;
        c.prerequisite_source="STATIC";
        c.prerequisites={"ZZ9999"};
        pagedGroup.candidates.push_back(c.code);
        pagedCourses[c.code]=c;
    }
    pagedReq.choice_groups={pagedGroup};
    // ZZ9999 is an unselected alternative, not a guaranteed planned course.
    pagedReq.choice_groups.push_back(ChoiceGroup{});
    pagedReq.choice_groups.back().candidates={"ZZ9999"};
    std::istringstream pagedInput("\n21\nM\nM\n21\n");
    std::ostringstream pagedOutput;
    pickerChosen.clear();
    oldInput=std::cin.rdbuf(pagedInput.rdbuf());
    oldOutput=std::cout.rdbuf(pagedOutput.rdbuf());
    const auto paged=selectElectives(pagedReq,pagedCourses,pickerChosen,
        {pagedGroup.candidates,{"ZZ9999"}},{true,false},{},false);
    std::cin.rdbuf(oldInput);
    std::cout.rdbuf(oldOutput);
    assert(paged.selections[0].course_codes==std::vector<std::string>{"ZZ1020"});
    assert(pagedOutput.str().find("All candidates are already shown")!=std::string::npos);
    assert(pagedOutput.str().find("ZZ9999 ⚠️ not listed")!=std::string::npos);
    assert(pagedOutput.str().find("Offering unconfirmed")!=std::string::npos);
    assert(pagedCourses.at("ZZ1020").semesters.empty());

    // Preferences accept lowercase, remove duplicates, reject conflicting none,
    // and permit returning to the previous question.
    std::istringstream preferenceInput("\nA,B\na,a\np\n");
    std::ostringstream preferenceOutput;
    oldInput=std::cin.rdbuf(preferenceInput.rdbuf());
    oldOutput=std::cout.rdbuf(preferenceOutput.rdbuf());
    const auto checkedAnswer=readMultiSelect("Preference",{"topic","none / all fine"},false);
    const auto previousAnswer=readMultiSelect("Assessment",{"presentation","none / all fine"},true);
    std::cin.rdbuf(oldInput);
    std::cout.rdbuf(oldOutput);
    assert(checkedAnswer.selections==std::vector<std::string>{"topic"});
    assert(previousAnswer.previous);
    assert(preferenceOutput.str().find("cannot be combined")!=std::string::npos);

       
    {
        DegreeRequirements audit;
        audit.in_progress = {"cs2310", "CS2310"};          // duplicate + different case
        audit.completed_courses = {"CS3402"};
        audit.required_courses = {"  LC0200A  "};           // spaces
        audit.major_courses = {"NOT-A-COURSE", "AB123"};    // invalid codes
        audit.non_degree_courses = {"GE1401"};
        ChoiceGroup g;
        g.candidates = {"CS2311", "CS2310", "XX9999YY"};    // duplicate + invalid suffix
        audit.choice_groups = {g};

        const auto codes = degreeWorksCourseCodes(audit);

        // Must be unique, upper-case, sorted, only valid course-code pattern
        assert(codes.size() == 5);
        assert(codes[0] == "CS2310");
        assert(codes[1] == "CS2311");
        assert(codes[2] == "CS3402");
        assert(codes[3] == "GE1401");
        assert(codes[4] == "LC0200A");
        // invalid codes must be filtered out
        assert(std::find(codes.begin(), codes.end(), "NOT-A-COURSE") == codes.end());
        assert(std::find(codes.begin(), codes.end(), "AB123") == codes.end());
        assert(std::find(codes.begin(), codes.end(), "XX9999YY") == codes.end());
    }


    {
        DegreeRequirements empty;
        const auto codes = degreeWorksCourseCodes(empty);
        assert(codes.empty());
    }
    std::cout<<"All tests passed\n";
    return 0;
}
