#include "excel.h"
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>

namespace {
std::string shellQuote(const std::string &value){
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
std::string jsonEscape(const std::string &s){
    std::string out;
    for(char c:s){
        if(c=='"'||c=='\\')out+='\\';
        if(c=='\n')out+="\\n";
        else if(c=='\r')out+="\\r";
        else out+=c;
    }
    return out;
}
void writeStringArray(std::ostream&out,const std::vector<std::string>&values){
    out<<"[";
    for(size_t i=0;i<values.size();++i){
        if(i)out<<",";
        out<<"\""<<jsonEscape(values[i])<<"\"";
    }
    out<<"]";
}
void writeStringMap(std::ostream&out,const std::unordered_map<std::string,std::string>&values){
    out<<"{";
    bool first=true;
    for(const auto&[key,value]:values){
        if(!first)out<<",";
        first=false;
        out<<"\""<<jsonEscape(key)<<"\":"<<"\""<<jsonEscape(value)<<"\"";
    }
    out<<"}";
}
}

bool exportExcel(const Plan &plan,const std::string &writerPath,
                 const std::string &pythonPath,
                 std::string &output,std::string &error){
    std::time_t t=std::time(nullptr);std::tm tm=*std::localtime(&t);
    char stamp[32];std::strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",&tm);
    const std::filesystem::path outputDirectory="Study Plans";
    std::error_code directoryError;
    std::filesystem::create_directories(outputDirectory,directoryError);
    if(directoryError){
        error="Could not create the Study Plans output folder: "+directoryError.message();
        return false;
    }
    output=std::filesystem::absolute(
        outputDirectory/("output_study_plan_"+std::string(stamp)+".xlsx")).string();
    std::ofstream j(".cityu_plan.json");
    if(!j){
        error="Could not create temporary study plan data in the application data directory.";
        return false;
    }
    j<<"{\"student_name\":\""<<jsonEscape(plan.student_name)
     <<"\",\"student_id\":\""<<jsonEscape(plan.student_id)
     <<"\",\"programme\":\""<<jsonEscape(plan.programme)
     <<"\",\"college\":\""<<jsonEscape(plan.college)
     <<"\",\"department\":\""<<jsonEscape(plan.department)
     <<"\",\"degree\":\""<<jsonEscape(plan.degree)
     <<"\",\"major\":\""<<jsonEscape(plan.major)
     <<"\",\"cohort\":\""<<jsonEscape(plan.cohort)
     <<"\",\"expected_graduation_date\":\""<<jsonEscape(plan.expected_graduation_date)
     <<"\",\"audit_date\":\""<<jsonEscape(plan.audit_date)
     <<"\",\"current_semester\":\""<<jsonEscape(plan.current_semester)
     <<"\",\"max_credits_permitted\":"<<plan.max_credits_permitted
     <<",\"plan_years\":"<<plan.plan_years
     <<",\"start_term_index\":"<<plan.start_term_index
     <<",\"courses\":[";
    for(size_t i=0;i<plan.courses.size();++i){
        const auto&p=plan.courses[i];if(i)j<<",";
        std::string place=p.year_semester;
        if(!p.second_year_semester.empty())
            place+=" ("+std::to_string(p.first_term_credits)+" + "+
                std::to_string(p.second_term_credits)+
                " credits; year-long, complete both terms)";
        std::string prerequisiteText=p.course.prerequisites_text;
        if(prerequisiteText.empty())
            for(size_t k=0;k<p.course.prerequisites.size();++k){
                if(k)prerequisiteText+=", ";
                prerequisiteText+=p.course.prerequisites[k];
            }
        std::string exclusiveText;
        for(size_t k=0;k<p.course.exclusive_courses.size();++k){
            if(k)exclusiveText+=", ";
            exclusiveText+=p.course.exclusive_courses[k];
        }
        j<<"{\"code\":\""<<jsonEscape(p.course.code)
         <<"\",\"title\":\""<<jsonEscape(p.course.title)
         <<"\",\"credits\":"<<p.course.credits
         <<",\"credits_known\":"<<(p.course.credits_known?"true":"false")
         <<",\"requirement\":\""
         <<jsonEscape(p.requirement)<<"\",\"elective_selection\":"
         <<(p.elective_selection?"true":"false")
         <<",\"counts_toward_degree\":"<<(p.counts_toward_degree?"true":"false")
         <<",\"manual_placement\":"<<(p.manual_placement?"true":"false")
          <<",\"place\":\""<<jsonEscape(place)<<"\",\"warning\":\""
         <<(p.warning?"⚠️":"")<<"\",\"term_credits\":"<<p.first_term_credits
         <<",\"second_place\":\""<<jsonEscape(p.second_year_semester)
         <<"\",\"second_term_credits\":"<<p.second_term_credits
         <<",\"semesters\":\"";
        for(size_t k=0;k<p.course.semesters.size();++k){if(k)j<<", ";j<<p.course.semesters[k];}
        j<<"\",\"assessment\":\"";
        if(p.course.assessment.exam_percent&&p.course.assessment.other_percent){
            if(p.course.assessment.unknown)j<<"Unconfirmed — ";
            j<<"Exam "<<*p.course.assessment.exam_percent<<"% / Continuous assessment "
             <<*p.course.assessment.other_percent<<"%";
            if(p.course.assessment.unknown&&!p.course.assessment.raw_text.empty())
                j<<" ("<<p.course.assessment.raw_text<<")";
        }
        else if(p.course.assessment.raw_items.size()){
            j<<"Unconfirmed";
            if(!p.course.assessment.raw_text.empty())j<<" — "<<p.course.assessment.raw_text;
            j<<"; components: ";
            for(size_t k=0;k<p.course.assessment.raw_items.size();++k){
                if(k)j<<"; ";
                j<<p.course.assessment.raw_items[k];
            }
        }else j<<"Unconfirmed";
        j<<"\",\"prerequisites\":\""<<jsonEscape(prerequisiteText)
         <<"\",\"exclusive_courses\":\""<<jsonEscape(exclusiveText)
         <<"\",\"catalogue_notes\":\""<<jsonEscape(p.course.catalogue_notes)
         <<"\",\"prerequisite_status\":\""<<prereqStatusText(p.course.prereq_status)
         <<"\",\"prerequisite_statuses\":";
        writeStringMap(j,p.prerequisite_statuses);
        j<<",\"url\":\""<<jsonEscape(p.course.url)<<"\",\"warnings\":";
        writeStringArray(j,p.warnings);
        j<<"}";
    }
    j<<"],\"illustrative\":"<<(plan.illustrative?"true":"false")
     <<",\"elective_selections\":[";
    for(size_t i=0;i<plan.elective_selections.size();++i){
        const auto&selection=plan.elective_selections[i];
        if(i)j<<",";
        j<<"{\"requirement\":\""<<jsonEscape(selection.requirement)
         <<"\",\"note\":\""<<jsonEscape(selection.note)<<"\",\"courses\":[";
        for(size_t k=0;k<selection.course_codes.size();++k){
            if(k)j<<",";
            j<<"\""<<jsonEscape(selection.course_codes[k])<<"\"";
        }
        j<<"]}";
    }
    j<<"],\"warnings\":";
    writeStringArray(j,plan.warnings);
    j<<"}";
    j.close();
    std::string cmd=shellQuote(pythonPath)+" "+shellQuote(writerPath)+
        " --plan "+shellQuote(".cityu_plan.json")+
        " --output "+shellQuote(output);
    int rc=std::system(cmd.c_str());
    if(rc!=0){error="Could not write the Excel workbook. Open "+output+" issues in the error log.";return false;}
    if(!std::filesystem::exists(output)||std::filesystem::file_size(output)==0){error="The generated Excel file was empty.";return false;}
    return true;
}
