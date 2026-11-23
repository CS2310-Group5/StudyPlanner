#include "models.h"

Course mergeCourseData(const Course&primary,const Course&fallback){
    Course result=primary;
    if(result.code.empty())result.code=fallback.code;
    if(result.title.empty())result.title=fallback.title;
    if(!result.credits_known&&fallback.credits_known){
        result.credits=fallback.credits;
        result.credits_known=true;
    }
    if(result.semesters.empty())result.semesters=fallback.semesters;
    if(result.assessment.unknown&&!fallback.assessment.unknown)
        result.assessment=fallback.assessment;
    if(result.prerequisite_source=="UNKNOWN"&&
       fallback.prerequisite_source!="UNKNOWN"){
        result.prerequisites=fallback.prerequisites;
        result.prerequisites_text=fallback.prerequisites_text;
        result.prerequisite_source=fallback.prerequisite_source;
    }
    if(result.exclusive_courses.empty())
        result.exclusive_courses=fallback.exclusive_courses;
    if(result.catalogue_notes.empty())
        result.catalogue_notes=fallback.catalogue_notes;
    if(result.catalogue_year.empty())
        result.catalogue_year=fallback.catalogue_year;
    if(!result.catalogue_detail_available&&
       fallback.catalogue_detail_available)
        result.catalogue_detail_available=true;
    if(result.area.empty())result.area=fallback.area;
    if(result.tags.empty())result.tags=fallback.tags;
    if(result.url.empty())result.url=fallback.url;
    if(result.assessment.raw_text.empty())
        result.assessment.raw_text=fallback.assessment.raw_text;
    if(result.assessment.raw_items.empty())
        result.assessment.raw_items=fallback.assessment.raw_items;
    return result;
}
