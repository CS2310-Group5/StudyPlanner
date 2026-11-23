#include "json.h"
#include <cctype>
#include <fstream>
#include <iostream>
#include <regex>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
class JsonReader {
    std::string s_;
    size_t p_ = 0;
    void ws() { while (p_ < s_.size() && std::isspace((unsigned char)s_[p_])) ++p_; }
    bool take(char c) { ws(); if (p_ < s_.size() && s_[p_] == c) { ++p_; return true; } return false; }
public:
    explicit JsonReader(std::string s) : s_(std::move(s)) {}
    std::string parseString() {
        ws(); if (p_ >= s_.size() || s_[p_] != '"') return "";
        ++p_; std::string out;
        while (p_ < s_.size()) {
            char c = s_[p_++]; if (c == '"') break;
            if (c == '\\' && p_ < s_.size()) {
                char e = s_[p_++];
                if (e == 'n') out += '\n'; else if (e == 'r') out += '\r';
                else if (e == 't') out += '\t'; else if (e == 'u') {
                    p_ += 4; out += '?';
                } else out += e;
            } else out += c;
        }
        return out;
    }
    std::vector<std::string> parseStringArray() {
        std::vector<std::string> out; ws(); if (!take('[')) return out;
        while (true) { ws(); if (take(']')) break; out.push_back(parseString()); if (take(',')) continue; if (take(']')) break; }
        return out;
    }
    bool parseObject(std::unordered_map<std::string, Course> &courses);
};

std::string getStringValue(const std::string &text, const std::string &key);
int getIntValue(const std::string &text, const std::string &key);
std::vector<std::string> extractArray(const std::string &text,const std::string &key);
}

std::string trimJson(const std::string &s) {
    size_t b=0,e=s.size(); while(b<e && isspace((unsigned char)s[b]))++b; while(e>b && isspace((unsigned char)s[e-1]))--e; return s.substr(b,e-b);
}

namespace {
std::string getStringValue(const std::string &text, const std::string &key) {
    std::string needle = "\"" + key + "\"";
    size_t p=text.find(needle); if(p==std::string::npos)return "";
    p=text.find(':',p); if(p==std::string::npos)return "";
    p++; while(p<text.size() && isspace((unsigned char)text[p]))++p;
    if(p>=text.size()||text[p]!='"')return "";
    ++p; std::string out;
    while(p<text.size()) { char c=text[p++]; if(c=='"' )break; if(c=='\\'&&p<text.size()){char e=text[p++];out+=(e=='n'?'\n':(e=='t'?'\t':e));}else out+=c;}
    return out;
}
bool hasKey(const std::string &text,const std::string &key){
    return text.find("\""+key+"\"")!=std::string::npos;
}
bool getBoolValue(const std::string &text,const std::string &key){
    const std::string marker="\""+key+"\"";
    size_t p=text.find(marker);
    if(p==std::string::npos)return false;
    p=text.find(':',p);
    if(p==std::string::npos)return false;
    return text.substr(p+1,8).find("true")!=std::string::npos;
}
std::vector<std::string> parsePrerequisites(const std::string &object,
                                            std::string &source){
    const std::string marker="\"prerequisites\"";
    size_t p=object.find(marker);
    if(p==std::string::npos){
        source="UNKNOWN";
        return {"UNKNOWN"};
    }
    p=object.find(':',p);
    if(p==std::string::npos){
        source="UNKNOWN";
        return {"UNKNOWN"};
    }
    ++p;
    while(p<object.size()&&std::isspace((unsigned char)object[p]))++p;
    if(p<object.size()&&object[p]=='['){
        auto values=extractArray(object,"prerequisites");
        source=std::find(values.begin(),values.end(),"UNKNOWN")!=values.end()?"UNKNOWN":"STATIC";
        return values;
    }
    if(p<object.size()&&object[p]=='"'){
        std::string raw=getStringValue(object,"prerequisites");
        std::string normalized=raw;
        for(char&c:normalized)c=(char)std::tolower((unsigned char)c);
        normalized=trimJson(normalized);
        std::vector<std::string> values;
        const std::regex codeRe("\\b[A-Z]{2,5}[ \t]?[0-9]{4}[A-Z]?\\b");
        for(std::sregex_iterator it(raw.begin(),raw.end(),codeRe),end;it!=end;++it){
            std::string code=it->str();
            code.erase(std::remove_if(code.begin(),code.end(),[](unsigned char c){return std::isspace(c);}),code.end());
            if(std::find(values.begin(),values.end(),code)==values.end())values.push_back(code);
        }
        if(!values.empty()){
            source="STATIC";
            return values;
        }
        if(normalized=="none"||normalized=="nil"||normalized=="no prerequisites"||
           normalized=="not applicable"||normalized=="n/a"){
            source="STATIC";
            return {};
        }
    }
    source="UNKNOWN";
    return {"UNKNOWN"};
}
int getIntValue(const std::string &text,const std::string &key) {
    std::string needle="\""+key+"\"";size_t p=text.find(needle);if(p==std::string::npos)return -1;
    p=text.find(':',p);if(p==std::string::npos)return -1;++p;while(p<text.size()&&isspace((unsigned char)text[p]))++p;
    try{return std::stoi(text.substr(p));}catch(...){return -1;}
}
std::vector<std::string> extractArray(const std::string &text,const std::string &key) {
    std::vector<std::string> out;std::string needle="\""+key+"\"";size_t p=text.find(needle);if(p==std::string::npos)return out;
    p=text.find('[',p);if(p==std::string::npos)return out;size_t q=text.find(']',p);if(q==std::string::npos)return out;
    std::string a=text.substr(p+1,q-p-1);size_t pos=0;
    while(pos<a.size()){size_t b=a.find('"',pos);if(b==std::string::npos)break;size_t e=a.find('"',b+1);if(e==std::string::npos)break;out.push_back(a.substr(b+1,e-b-1));pos=e+1;}
    return out;
}
std::string escapeJson(const std::string &value){
    std::ostringstream out;
    for(unsigned char ch:value){
        switch(ch){
            case '"':out<<"\\\"";break;
            case '\\':out<<"\\\\";break;
            case '\b':out<<"\\b";break;
            case '\f':out<<"\\f";break;
            case '\n':out<<"\\n";break;
            case '\r':out<<"\\r";break;
            case '\t':out<<"\\t";break;
            default:
                if(ch<0x20)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<int>(ch)<<std::dec;
                else out<<static_cast<char>(ch);
        }
    }
    return out.str();
}
void writeStringArray(std::ostream&out,const std::vector<std::string>&values){
    out<<"[";
    for(size_t i=0;i<values.size();++i){
        if(i)out<<", ";
        out<<"\""<<escapeJson(values[i])<<"\"";
    }
    out<<"]";
}
}

bool loadCoursesDb(const std::string &path,
                   std::unordered_map<std::string, Course> &courses,
                   std::string &error) {
    std::ifstream f(path); if(!f){error="Could not open "+path+".";return false;}
    std::string all((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    size_t pos=0;
    while(pos<all.size()){
        size_t codePos=all.find('"',pos); if(codePos==std::string::npos)break;
        size_t codeEnd=all.find('"',codePos+1); if(codeEnd==std::string::npos)break;
        std::string code=all.substr(codePos+1,codeEnd-codePos-1);
        size_t obj=all.find('{',codeEnd); if(obj==std::string::npos)break;
        size_t brace=obj,depth=0,end=obj;
        for(;end<all.size();++end){if(all[end]=='{')++depth;else if(all[end]=='}'){--depth;if(depth==0){++end;break;}}}
        std::string objText=all.substr(obj,end-obj);
        if(code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")!=std::string::npos){pos=end;continue;}
        Course c;c.code=code;c.title=getStringValue(objText,"title");c.credits=getIntValue(objText,"credits");
        c.credits_known=c.credits>=0;
        if(c.credits<0)c.credits=3;
        c.semesters=extractArray(objText,"semesters");
        for(auto&term:c.semesters){
            if(term=="A")term="Semester A";
            else if(term=="B")term="Semester B";
            else if(term=="S")term="Summer";
            else if(term=="X")term="Not offered";
        }
        c.prerequisites=parsePrerequisites(objText,c.prerequisite_source);
        c.prerequisites_text=getStringValue(objText,"prerequisites_text");
        c.exclusive_courses=extractArray(objText,"exclusive_courses");
        c.catalogue_notes=getStringValue(objText,"catalogue_notes");
        c.catalogue_year=getStringValue(objText,"catalogue_year");
        c.catalogue_detail_available=getBoolValue(objText,"catalogue_detail_available");
        c.area=getStringValue(objText,"area");c.tags=extractArray(objText,"tags");c.url=getStringValue(objText,"url");
        if(getBoolValue(objText,"has_group_work")&&
           std::find(c.tags.begin(),c.tags.end(),"group-work")==c.tags.end())
            c.tags.push_back("group-work");
        if(getBoolValue(objText,"has_exam")&&
           std::find(c.tags.begin(),c.tags.end(),"exam-indicated")==c.tags.end())
            c.tags.push_back("exam-indicated");
        if(c.url.empty())c.url="https://www.cityu.edu.hk/catalogue/ug/current/course/"+code+".htm";
        c.assessment.raw_items=extractArray(objText,"raw_items");
        c.assessment.raw_text=getStringValue(objText,"raw_text");
        int exam=getIntValue(objText,"exam_percent");
        int other=getIntValue(objText,"other_percent");
        if(other<0)other=getIntValue(objText,"ca_percent");
        if(exam>=0&&exam<=100)c.assessment.exam_percent=exam;
        if(other>=0&&other<=100)c.assessment.other_percent=other;
        const bool assessmentUnknownDeclared=getBoolValue(objText,"assessment_unknown");
        const bool hasAssessmentUnknown=hasKey(objText,"assessment_unknown");
        c.assessment.unknown=true;
        if(c.assessment.exam_percent&&*c.assessment.exam_percent==100&&!c.assessment.other_percent){
            c.assessment.other_percent=0;
        }else if(c.assessment.other_percent&&*c.assessment.other_percent==100&&!c.assessment.exam_percent){
            c.assessment.exam_percent=0;
        }
        const bool completeSplit=c.assessment.exam_percent&&c.assessment.other_percent&&
            *c.assessment.exam_percent+*c.assessment.other_percent==100;
        c.assessment.unknown=(hasAssessmentUnknown&&assessmentUnknownDeclared)||!completeSplit;
        courses[code]=c;pos=end;
    }
    return true;
}

bool loadGatewayEducationAreas(
        const std::string &path,
        std::unordered_map<std::string,std::string> &areas,
        std::string &error){
    std::ifstream file(path);
    if(!file){error="Could not open "+path+".";return false;}
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    const std::regex areaPattern(
        R"re("Area\s+([1-3])"\s*:\s*\[([^\]]*)\])re");
    const std::regex coursePattern(R"re("((?:GE)\d{4}[A-Z]?)")re");
    std::unordered_map<std::string,std::string> loaded;
    for(std::sregex_iterator areaIt(text.begin(),text.end(),areaPattern),end;
        areaIt!=end;++areaIt){
        const std::string area="Area "+(*areaIt)[1].str();
        const std::string list=(*areaIt)[2].str();
        for(std::sregex_iterator courseIt(list.begin(),list.end(),coursePattern),
            courseEnd;courseIt!=courseEnd;++courseIt){
            const std::string code=(*courseIt)[1].str();
            const auto existing=loaded.find(code);
            if(existing!=loaded.end()&&existing->second!=area){
                error="Course "+code+" is assigned to more than one Gateway Education area.";
                return false;
            }
            loaded[code]=area;
        }
    }
    if(loaded.empty()){
        error="No Gateway Education area mappings were found in "+path+".";
        return false;
    }
    areas=std::move(loaded);
    return true;
}

bool mergeBundledCourseDb(const std::string &bundledPath,
                          const std::string &personalPath,
                          std::string &error){
    std::unordered_map<std::string,Course> bundled,personal;
    if(!loadCoursesDb(bundledPath,bundled,error))return false;
    if(!loadCoursesDb(personalPath,personal,error))return false;
    bool changed=false;
    for(const auto &[code,incoming]:bundled){
        const auto existing=personal.find(code);
        if(existing==personal.end()){
            personal.emplace(code,incoming);
            changed=true;
            continue;
        }
        const bool newerCatalogue=!incoming.catalogue_year.empty()&&
            incoming.catalogue_year>existing->second.catalogue_year;
        const bool newerPersonal=!existing->second.catalogue_year.empty()&&
            !incoming.catalogue_year.empty()&&
            existing->second.catalogue_year>incoming.catalogue_year;
        if(incoming.catalogue_detail_available&&
           !newerPersonal&&
           (!existing->second.catalogue_detail_available||newerCatalogue)){
            existing->second=incoming;
            changed=true;
        }else if(!newerPersonal&&
                 incoming.prerequisite_source!="UNKNOWN"&&
                 existing->second.prerequisite_source=="UNKNOWN"){
            existing->second.prerequisites=incoming.prerequisites;
            existing->second.prerequisites_text=incoming.prerequisites_text;
            existing->second.prerequisite_source=incoming.prerequisite_source;
            changed=true;
        }else if(existing->second.exclusive_courses.empty()&&
                 !incoming.exclusive_courses.empty()){
            existing->second.exclusive_courses=incoming.exclusive_courses;
            changed=true;
        }
        if(existing->second.catalogue_notes.empty()&&
           !incoming.catalogue_notes.empty()){
            existing->second.catalogue_notes=incoming.catalogue_notes;
            changed=true;
        }
    }
    if(!changed)return true;
    return saveCoursesDb(personalPath,personal,error);
}

bool saveCoursesDb(const std::string &path,
                   const std::unordered_map<std::string,Course> &courses,
                   std::string &error){
    const std::string temporary=path+".tmp";
    std::vector<std::string> codes;
    codes.reserve(courses.size());
    for(const auto &[code,course]:courses)codes.push_back(code);
    std::sort(codes.begin(),codes.end());
    std::ofstream out(temporary,std::ios::trunc);
    if(!out){error="Could not write temporary course database "+temporary+".";return false;}
    out<<"{\n";
    for(size_t i=0;i<codes.size();++i){
        const auto&course=courses.at(codes[i]);
        out<<"  \""<<escapeJson(codes[i])<<"\": {\n"
           <<"    \"title\": \""<<escapeJson(course.title)<<"\",\n"
           <<"    \"credits\": "<<(course.credits_known?std::to_string(course.credits):"null")<<",\n"
           <<"    \"semesters\": ";
        writeStringArray(out,course.semesters);
        out<<",\n    \"prerequisites\": ";
        writeStringArray(out,course.prerequisites);
        out<<",\n    \"prerequisites_text\": \""<<escapeJson(course.prerequisites_text)<<"\",\n"
           <<"    \"exclusive_courses\": ";
        writeStringArray(out,course.exclusive_courses);
        out<<",\n    \"catalogue_notes\": \""<<escapeJson(course.catalogue_notes)<<"\",\n"
           <<"    \"catalogue_year\": \""<<escapeJson(course.catalogue_year)<<"\",\n"
           <<"    \"catalogue_detail_available\": "<<(course.catalogue_detail_available?"true":"false")<<",\n"
           <<"    \"area\": \""<<escapeJson(course.area)<<"\",\n"
           <<"    \"tags\": ";
        writeStringArray(out,course.tags);
        out<<",\n    \"url\": \""<<escapeJson(course.url)<<"\",\n"
           <<"    \"exam_percent\": ";
        if(course.assessment.exam_percent)out<<*course.assessment.exam_percent;
        else out<<"null";
        out<<",\n    \"other_percent\": ";
        if(course.assessment.other_percent)out<<*course.assessment.other_percent;
        else out<<"null";
        out<<",\n    \"raw_items\": ";
        writeStringArray(out,course.assessment.raw_items);
        out<<",\n    \"raw_text\": \""<<escapeJson(course.assessment.raw_text)<<"\",\n"
           <<"    \"assessment_unknown\": "<<(course.assessment.unknown?"true":"false")<<"\n"
           <<"  }"<<(i+1<codes.size()?",":"")<<"\n";
    }
    out<<"}\n";
    out.close();
    if(!out){
        error="Could not finish writing temporary course database "+temporary+".";
        std::error_code cleanupError;
        std::filesystem::remove(temporary,cleanupError);
        return false;
    }
#ifdef _WIN32
    const std::filesystem::path sourcePath(temporary),destinationPath(path);
    if(!MoveFileExW(sourcePath.c_str(),destinationPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        error="Could not replace course database "+path+": "+
              std::error_code(static_cast<int>(GetLastError()),std::system_category()).message();
        std::error_code cleanupError;
        std::filesystem::remove(temporary,cleanupError);
        return false;
    }
#else
    std::error_code renameError;
    std::filesystem::rename(temporary,path,renameError);
    if(renameError){
        error="Could not replace course database "+path+": "+renameError.message();
        std::error_code cleanupError;
        std::filesystem::remove(temporary,cleanupError);
        return false;
    }
#endif
    return true;
}
