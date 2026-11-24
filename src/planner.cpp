#include "planner.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <functional>
#include <set>
#include <regex>

namespace {
std::string offeringTerm(const std::string&label){
    // Year/tentative suffixes describe the catalogue entry, not another term.
    // Keep unknown labels intact so the existing provisional fallback applies.
    static const std::regex termPrefix(
        R"(^\s*(Semester\s+A|Semester\s+B|Summer)\b)",std::regex::icase);
    std::smatch match;
    if(!std::regex_search(label,match,termPrefix))return label;
    std::string term=match[1].str();
    term.erase(std::remove_if(term.begin(),term.end(),
        [](unsigned char ch){return std::isspace(ch);}),term.end());
    std::transform(term.begin(),term.end(),term.begin(),
        [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
    if(term=="semestera")return "Semester A";
    if(term=="semesterb")return "Semester B";
    return "Summer";
}

bool summerOnlyOffering(const Course&course){
    bool summer=false;
    for(const auto&label:course.semesters){
        const auto term=offeringTerm(label);
        if(term=="Summer")summer=true;
        else if(term!="Not offered"&&term!="X")return false;
    }
    return summer;
}
}

bool hasSchedulableOffering(const Course&course){
    return hasOfferingInTerm(course,"Semester A")||
           hasOfferingInTerm(course,"Semester B");
}

bool hasOfferingInTerm(const Course&course,const std::string&term){
    const bool summer=term.find("Summer")!=std::string::npos;
    std::vector<std::string> offerings;
    for(const auto&label:course.semesters)offerings.push_back(offeringTerm(label));
    if(offerings.empty())return !summer;
    if(std::find(offerings.begin(),offerings.end(),"Any term")!=
       offerings.end())return !summer;
    if(summer)
        return std::find(offerings.begin(),offerings.end(),"Summer")!=
               offerings.end();
    const std::string semester=term.find("Semester B")!=std::string::npos
        ?"Semester B":"Semester A";
    for(const auto&offering:offerings){
        if(offering==semester||offering=="Not offered"||offering=="X")
            return true;
        if(offering!="Summer"&&offering!="Semester A"&&offering!="Semester B")
            return true;
    }
    return false;
}

bool hasUsableChoiceCourse(const Course&course,bool requiresCredits){
    const int credits=course.credits_known?course.credits:3;
    return credits>=0&&(!requiresCredits||credits>0)&&
        hasSchedulableOffering(course);
}

static int choiceCredits(const Course&course){
    return course.credits_known?course.credits:3;
}

static int areaForCourse(const Course&course,const std::vector<int>&areas){
    auto containsArea=[](const std::string&value,int area){
        std::string normalized=value;
        std::transform(normalized.begin(),normalized.end(),normalized.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        normalized.erase(std::remove_if(normalized.begin(),normalized.end(),
            [](unsigned char ch){return std::isspace(ch);}),normalized.end());
        return normalized.find("area"+std::to_string(area))!=std::string::npos;
    };
    for(int area:areas){
        if(containsArea(course.area,area))return area;
        for(const auto&tag:course.tags)if(containsArea(tag,area))return area;
    }
    return 0;
}

static bool canSatisfyChoiceGroupSingle(
                           const ChoiceGroup&group,
                           const std::vector<std::string>&candidates,
                           const std::unordered_map<std::string,Course>&courses){
    const auto belongsToChoiceArea=[&](const Course&course){
        if(group.areas.empty())return true;
        std::string normalizedArea=course.area;
        std::transform(normalizedArea.begin(),normalizedArea.end(),normalizedArea.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        normalizedArea.erase(std::remove_if(normalizedArea.begin(),normalizedArea.end(),
            [](unsigned char ch){return std::isspace(ch);}),normalizedArea.end());
        for(int area:group.areas)
            if(normalizedArea.find("area"+std::to_string(area))!=std::string::npos)
                return true;
        for(const auto&tag:course.tags){
            std::string normalizedTag=tag;
            std::transform(normalizedTag.begin(),normalizedTag.end(),normalizedTag.begin(),
                [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
            normalizedTag.erase(std::remove_if(normalizedTag.begin(),normalizedTag.end(),
                [](unsigned char ch){return std::isspace(ch);}),normalizedTag.end());
            for(int area:group.areas)
                if(normalizedTag.find("area"+std::to_string(area))!=std::string::npos)
                    return true;
        }
        return false;
    };
    if(group.required_credits>0){
        if(group.choose>0&&static_cast<size_t>(group.choose)>candidates.size())
            return false;
        long long maximumCredits=0;
        for(const auto&code:candidates){
            const auto course=courses.find(code);
            if(course==courses.end()||!hasUsableChoiceCourse(course->second,true))
                return false;
            if(!belongsToChoiceArea(course->second))return false;
            maximumCredits+=choiceCredits(course->second);
        }
        if(group.required_credits>maximumCredits)return false;
        const int maxCourses=group.choose>0
            ?group.choose
            :static_cast<int>(candidates.size());
        std::vector<std::vector<bool>> achievable(
            static_cast<size_t>(maxCourses+1),
            std::vector<bool>(static_cast<size_t>(group.required_credits+1),false));
        achievable[0][0]=true;
        int considered=0;
        for(const auto&code:candidates){
            const auto&course=courses.at(code);
            ++considered;
            const int credits=choiceCredits(course);
            for(int count=std::min(considered,maxCourses);count>0;--count)
                for(int total=group.required_credits;total>=credits;--total)
                    achievable[static_cast<size_t>(count)][static_cast<size_t>(total)]=
                        achievable[static_cast<size_t>(count)][static_cast<size_t>(total)]||
                        achievable[static_cast<size_t>(count-1)]
                                  [static_cast<size_t>(total-credits)];
        }
        if(group.choose>0){
            if(!achievable[static_cast<size_t>(group.choose)]
                          [static_cast<size_t>(group.required_credits)])
                return false;
        }else if(!std::any_of(achievable.begin(),achievable.end(),
            [&group](const std::vector<bool>&totals){
                return totals[static_cast<size_t>(group.required_credits)];
            }))return false;
        if(group.minimum_per_area_credits>0&&!group.areas.empty()){
            std::set<std::vector<int>> states;
            states.insert(std::vector<int>(group.areas.size()+2,0));
            for(const auto&code:candidates){
                const Course&course=courses.at(code);
                const int area=areaForCourse(course,group.areas);
                if(area==0)continue;
                const auto areaIt=std::find(group.areas.begin(),group.areas.end(),area);
                const size_t areaIndex=static_cast<size_t>(areaIt-group.areas.begin())+2;
                auto nextStates=states;
                for(const auto&state:states){
                    const int credits=choiceCredits(course);
                    const int total=state[0]+credits;
                    const int count=state[1]+1;
                    if(total>group.required_credits||
                       (group.choose>0&&count>group.choose))continue;
                    auto next=state;
                    next[0]=total;
                    next[1]=count;
                    next[areaIndex]=std::min(group.minimum_per_area_credits,
                        next[areaIndex]+credits);
                    nextStates.insert(std::move(next));
                }
                states=std::move(nextStates);
            }
            const int requiredAreas=(group.required_credits+
                group.minimum_per_area_credits-1)/group.minimum_per_area_credits;
            const bool areaRequirementMet=std::any_of(states.begin(),states.end(),
                [&](const std::vector<int>&state){
                    if(state[0]!=group.required_credits||
                       (group.choose>0&&state[1]!=group.choose))return false;
                    int met=0;
                    for(size_t i=2;i<state.size();++i)
                        if(state[i]>=group.minimum_per_area_credits)++met;
                    return met>=requiredAreas;
                });
            if(!areaRequirementMet)return false;
        }
        return true;
    }
    return group.choose>0&&static_cast<size_t>(group.choose)<=candidates.size()&&
        std::all_of(candidates.begin(),candidates.end(),[&](const std::string&code){
            const auto course=courses.find(code);
            return course!=courses.end()&&hasUsableChoiceCourse(course->second,false)&&
                   belongsToChoiceArea(course->second);
        });
}

bool canSatisfyChoiceGroup(const ChoiceGroup&group,
                           const std::vector<std::string>&candidates,
                           const std::unordered_map<std::string,Course>&courses){
    if(group.candidate_sets.empty())
        return canSatisfyChoiceGroupSingle(group,candidates,courses);
    ChoiceGroup singlePath=group;
    singlePath.candidate_sets.clear();
    for(const auto&candidateSet:group.candidate_sets){
        std::vector<std::string> pathCandidates;
        for(const auto&code:candidates)
            if(std::find(candidateSet.begin(),candidateSet.end(),code)!=
               candidateSet.end())
                pathCandidates.push_back(code);
        if(canSatisfyChoiceGroupSingle(singlePath,pathCandidates,courses))
            return true;
    }
    return false;
}

void classifyPrerequisites(DegreeRequirements &req,
                           std::unordered_map<std::string,Course> &courses){
    std::unordered_set<std::string> completed;
    completed.insert(req.completed_courses.begin(),req.completed_courses.end());
    std::unordered_set<std::string> studying(
        req.in_progress.begin(),req.in_progress.end());
    std::unordered_set<std::string> required(
        req.required_courses.begin(),req.required_courses.end());
    required.insert(req.major_courses.begin(),req.major_courses.end());
    for(const auto&group:req.choice_groups)
        for(const auto&candidate:group.candidates)
            required.erase(candidate);
    for(auto &[code,c]:courses){
        c.prereq_status=PrereqStatus::UNKNOWN;
        if(c.prerequisite_source=="UNKNOWN"||
           std::find(c.prerequisites.begin(),c.prerequisites.end(),"UNKNOWN")!=c.prerequisites.end()){
            c.prereq_status=PrereqStatus::UNKNOWN;
        }
        else if(c.prerequisites.empty())c.prereq_status=PrereqStatus::SATISFIED;
        else {
            bool allDone=true,allKnown=true;
            allDone=!c.prerequisites.empty();
            for(const auto&p:c.prerequisites){
                if(completed.count(p)){continue;}
                allDone=false;
                if(!required.count(p)&&!studying.count(p))allKnown=false;
            }
            if(allDone)c.prereq_status=PrereqStatus::SATISFIED;
            else if(allKnown)c.prereq_status=PrereqStatus::SCHEDULED;
            else c.prereq_status=PrereqStatus::UNSCHEDULED;
        }
    }
}

Plan makePlan(const DegreeRequirements &req,
              const std::unordered_map<std::string,Course> &courses,
              const std::unordered_map<std::string,Course> &chosen,
              const Config &config,
              bool advancedStanding){
    Plan plan;
    plan.illustrative=req.illustrative;
    plan.plan_years=std::clamp(req.remaining_years>0?req.remaining_years:
                               (advancedStanding?3:4),1,4);
    plan.current_semester=req.current_semester.empty()?"Semester A":req.current_semester;
    plan.start_term_index=1;
    if(req.in_progress.empty()&&!req.illustrative)
        plan.warnings.push_back(
            "No courses were found in the Degree Works In-progress section; Year 1 Semester A is left empty.");
    if(!req.illustrative&&req.in_progress.empty()&&
       req.required_courses.empty()&&req.choice_groups.empty())
        plan.warnings.push_back(
            "No remaining courses or choice requirements were detected in Degree Works; verify that the report was parsed correctly.");
    if(req.conflicting_in_progress_terms)
        plan.warnings.push_back(
            "In-progress rows have differing term labels; all listed courses are placed in Year 1 Semester A as requested.");
    plan.student_name=req.student_name;
    plan.student_id=req.student_id;
    plan.programme=req.programme;
    plan.college=req.college;
    plan.department=req.department;
    plan.degree=req.degree;
    plan.major=req.major;
    plan.cohort=req.cohort;
    plan.expected_graduation_date=req.expected_graduation_date;
    plan.audit_date=req.audit_date;
    plan.max_credits_permitted=req.max_credits_permitted;
    plan.audit_remaining_credits=req.audit_remaining_credits;

    std::vector<std::string> seed;
    const auto addSeed=[&](const std::string&code){
        if(std::find(seed.begin(),seed.end(),code)==seed.end())seed.push_back(code);
    };
    for(const auto&code:req.major_courses)addSeed(code);
    for(const auto&code:req.required_courses)addSeed(code);
    for(const auto &[code,course]:chosen)
        addSeed(code);

    const std::unordered_set<std::string> completedAudit(
        req.completed_courses.begin(),req.completed_courses.end());
    std::vector<std::string> order=req.in_progress;
    std::unordered_set<std::string> visited;
    std::function<void(const std::string&)> visit=[&](const std::string&code){
        if(completedAudit.count(code))return;
        if(visited.count(code))return;
        visited.insert(code);
        Course course;
        bool haveCourse=false;
        auto it=courses.find(code);
        if(it!=courses.end()){course=it->second;haveCourse=true;}
        else{
            auto chosenIt=chosen.find(code);
            if(chosenIt!=chosen.end()){course=chosenIt->second;haveCourse=true;}
        }
        if(haveCourse)for(const auto&prereq:course.prerequisites)
            if(std::find(seed.begin(),seed.end(),prereq)!=seed.end())visit(prereq);
        if(std::find(order.begin(),order.end(),code)==order.end())
            order.push_back(code);
    };
    for(const auto&code:seed)visit(code);
    const std::unordered_set<std::string> included(order.begin(),order.end());
    std::set<std::pair<std::string,std::string>> reportedExclusions;
    for(const auto&code:order){
        const auto course=courses.find(code);
        const auto selected=chosen.find(code);
        const Course*details=course!=courses.end()?&course->second:
            (selected!=chosen.end()?&selected->second:nullptr);
        if(!details)continue;
        for(const auto&exclusive:details->exclusive_courses){
            if(!included.count(exclusive))continue;
            const auto pair=std::minmax(code,exclusive);
            if(reportedExclusions.emplace(pair.first,pair.second).second)
                plan.issues.push_back(code+" and "+exclusive+
                    " are listed as exclusive courses; verify that both should not count toward this plan.");
        }
    }

    std::unordered_set<std::string> completed(
        req.completed_courses.begin(),req.completed_courses.end());
    completed.insert(req.completed_courses.begin(),req.completed_courses.end());
    const std::unordered_set<std::string> required(
        req.required_courses.begin(),req.required_courses.end());
    const std::unordered_set<std::string> majorRequired(
        req.major_courses.begin(),req.major_courses.end());
    const std::unordered_set<std::string> nonDegree(
        req.non_degree_courses.begin(),req.non_degree_courses.end());
    std::unordered_set<std::string> plannedCourses(
        req.required_courses.begin(),req.required_courses.end());
    plannedCourses.insert(req.major_courses.begin(),req.major_courses.end());
    for(const auto&group:req.choice_groups)
        plannedCourses.insert(group.candidates.begin(),group.candidates.end());
    const std::unordered_set<std::string> studying(
        req.in_progress.begin(),req.in_progress.end());
    const auto prerequisiteStatuses=[&](const Course&course){
        std::unordered_map<std::string,std::string> statuses;
        for(const auto&prerequisite:course.prerequisites){
            if(completed.count(prerequisite))
                statuses[prerequisite]="studied";
            else if(studying.count(prerequisite))
                statuses[prerequisite]="studying";
            else if(plannedCourses.count(prerequisite))
                statuses[prerequisite]="planned";
            else
                statuses[prerequisite]="not listed";
        }
        return statuses;
    };
    std::vector<std::string> terms;
    for(int year=1;year<=plan.plan_years;++year){
        terms.push_back("Year "+std::to_string(year)+" Semester A");
        terms.push_back("Year "+std::to_string(year)+" Semester B");
        terms.push_back("Year "+std::to_string(year)+" Summer");
    }
    const int creditLimit=std::max(1,advancedStanding?
        config.advanced_standing_max_credits:config.default_max_credits);
    const int defaultTarget=advancedStanding?creditLimit:
        std::min(18,creditLimit);
    std::unordered_map<std::string,int> inProgressCreditByCode;
    for(const auto&detail:req.in_progress_details)
        inProgressCreditByCode[detail.code]=detail.credits_known
            ?detail.credits:3;
    int knownRemainingCredits=0;
    std::vector<bool> availableTerms(terms.size(),false);
    for(const auto&code:order){
        if(studying.count(code)||nonDegree.count(code))continue;
        const auto courseIt=courses.find(code);
        const auto chosenIt=chosen.find(code);
        const Course*course=courseIt!=courses.end()?&courseIt->second:
            (chosenIt!=chosen.end()?&chosenIt->second:nullptr);
        const int credits=course&&course->credits_known?course->credits:3;
        knownRemainingCredits+=credits;
        if(!course)continue;
        const std::string label=course->title+" "+code;
        std::string lowered=label;
        std::transform(lowered.begin(),lowered.end(),lowered.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        if(lowered.find("internship")!=std::string::npos)continue;
        if(summerOnlyOffering(*course))continue;
        for(size_t term=static_cast<size_t>(plan.start_term_index);
            term<terms.size();++term)
            if(term%3!=2&&hasOfferingInTerm(*course,terms[term]))
                availableTerms[term]=true;
    }
    for(const auto&code:req.in_progress)
        if(!nonDegree.count(code)){
            auto credits=inProgressCreditByCode.find(code);
            if(credits!=inProgressCreditByCode.end())
                plan.in_progress_credits+=credits->second;
            else{
                const auto course=courses.find(code);
                plan.in_progress_credits+=course!=courses.end()&&
                    course->second.credits_known?course->second.credits:3;
            }
        }
    plan.remaining_credits=req.audit_remaining_credits>0
        ?std::max(0,req.audit_remaining_credits-plan.in_progress_credits)
        :knownRemainingCredits;
    const int averageBasis=std::max(plan.remaining_credits,
                                    knownRemainingCredits);
    const auto availableTermCount=static_cast<int>(std::count(
        availableTerms.begin(),availableTerms.end(),true));
    plan.average_credits_per_term=availableTermCount>0&&averageBasis>0
        ?(averageBasis+availableTermCount-1)/availableTermCount
        :defaultTarget;
    const int targetCredits=std::clamp(plan.average_credits_per_term,1,creditLimit);
    std::vector<int> termCredits(terms.size(),0);
    std::unordered_map<std::string,size_t> scheduledTerm;
    std::unordered_set<std::string> manualPlacementCodes;
    std::unordered_set<std::string> reportedUnknownCredits;
    bool assumedOfferingTerms=false;
    bool courseWarnings=false;
    const auto getCourse=[&](const std::string&code){
        auto it=courses.find(code);
        if(it!=courses.end())return it->second;
        auto chosenIt=chosen.find(code);
        if(chosenIt!=chosen.end())return chosenIt->second;
        Course course;
        course.code=code;
        course.credits=3;
        return course;
    };
    const auto conflictsWithEnglishCourse=[&](const std::string&code,size_t term){
        if(code!="LC0200A"&&code!="LC0200B")return false;
        const auto partner=scheduledTerm.find(code=="LC0200A"?"LC0200B":"LC0200A");
        return partner!=scheduledTerm.end()&&partner->second==term;
    };
    for(const std::string&code:order){
        Course course=getCourse(code);
        std::string courseLabel=course.title+" "+code;
        std::transform(courseLabel.begin(),courseLabel.end(),courseLabel.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        const bool internship=courseLabel.find("internship")!=std::string::npos;
        const bool project=courseLabel.find("project")!=std::string::npos;
        const bool inProgress=std::find(req.in_progress.begin(),req.in_progress.end(),code)!=req.in_progress.end();
        size_t earliest=static_cast<size_t>(plan.start_term_index);
        for(const auto&prereq:course.prerequisites){
            auto term=scheduledTerm.find(prereq);
            if(term!=scheduledTerm.end())earliest=std::max(earliest,term->second+1);
        }
        if(!course.credits_known&&reportedUnknownCredits.insert(code).second)
            plan.warnings.push_back("Credit units for "+code+
                                    " are missing; 3 credits are assumed.");
        int firstTermCredits=course.credits_known?course.credits:3;
        int secondTermCredits=0;
        if(firstTermCredits+secondTermCredits!=course.credits)
            if(course.credits_known)
                plan.warnings.push_back("Semester credit parts for "+code+
                                        " do not add up to its full course credit.");

        const auto offeredIn=[&](size_t term){
            // Summer remains a manual-planning column, even if offered.
            return term%3!=2&&(inProgress||hasOfferingInTerm(course,terms[term]));
        };
        const bool summerOnly=summerOnlyOffering(course);
        const bool manualPrerequisite=std::any_of(
            course.prerequisites.begin(),course.prerequisites.end(),
            [&manualPlacementCodes](const std::string&prerequisite){
                return manualPlacementCodes.count(prerequisite)>0;
            });
        if(!inProgress&&(internship||summerOnly||manualPrerequisite)){
            PlanCourse item;
            item.course=course;
            item.prerequisite_statuses=prerequisiteStatuses(course);
            item.year_semester="Summer (manual placement)";
            item.manual_placement=true;
            item.counts_toward_degree=!nonDegree.count(code);
            item.first_term_credits=firstTermCredits;
            item.warning=true;
            item.warnings.push_back(internship
                ?"Place this Internship in Summer manually."
                :summerOnly
                    ?"Only a Summer offering is listed; choose its term manually."
                    :"A prerequisite needs manual placement; verify the sequence.");
            if(completed.count(code))item.requirement="Completed";
            else if(majorRequired.count(code))
                item.requirement="Degree Works major requirement";
            else if(required.count(code))
                item.requirement="Degree Works required";
            else if(chosen.count(code)){
                item.requirement="Elective selection";
                item.elective_selection=true;
            }else item.requirement="Prerequisite";
            if(!course.credits_known)
                item.warnings.push_back("Credit units are missing; 3 credits are assumed.");
            manualPlacementCodes.insert(code);
            plan.courses.push_back(std::move(item));
            continue;
        }
        size_t assigned=terms.size();
        if(inProgress){
            assigned=0;
            if(conflictsWithEnglishCourse(code,assigned))
                plan.warnings.push_back("Degree Works lists "+code+
                    " in progress in Year 1 Semester A, where LC0200A and LC0200B cannot both be scheduled.");
        }else if(project){
            const size_t lastYearStart=static_cast<size_t>((plan.plan_years-1)*3);
            const auto selectBalancedTerm=[&](size_t begin,int maxCredits){
                size_t best=terms.size();
                for(size_t term=begin;term<terms.size();++term){
                    if(!offeredIn(term)||conflictsWithEnglishCourse(code,term)||
                       termCredits[term]+firstTermCredits>maxCredits)continue;
                    if(best==terms.size()||termCredits[term]<termCredits[best])
                        best=term;
                }
                return best;
            };
            assigned=selectBalancedTerm(std::max(earliest,lastYearStart),targetCredits);
            if(assigned==terms.size())
                assigned=selectBalancedTerm(lastYearStart,creditLimit);
            if(assigned==terms.size()){
                for(size_t term=lastYearStart;term<terms.size();++term)
                    if(terms[term].find("Summer")==std::string::npos){
                        assigned=term;
                        break;
                    }
                plan.warnings.push_back("Project course "+code+
                    " is placed in the final year, but its prerequisites or confirmed offering terms may conflict.");
            }
        }else{
            const auto selectBalancedTerm=[&](int maxCredits){
                size_t best=terms.size();
                for(size_t term=earliest;term<terms.size();++term){
                    if(!offeredIn(term)||conflictsWithEnglishCourse(code,term)||
                       termCredits[term]+firstTermCredits>maxCredits)continue;
                    if(best==terms.size()||termCredits[term]<termCredits[best])
                        best=term;
                }
                return best;
            };
            assigned=selectBalancedTerm(targetCredits);
            if(assigned==terms.size())assigned=selectBalancedTerm(creditLimit);
            if(assigned==terms.size())
                for(size_t term=earliest;term<terms.size();++term)
                    if(offeredIn(term)&&!conflictsWithEnglishCourse(code,term)){
                        assigned=term;
                        plan.warnings.push_back("The credit limit could not be met for "+code+
                            "; the course is placed in the earliest available term.");
                        break;
                    }
            if(assigned==terms.size()){
                for(size_t term=earliest;term<terms.size();++term)
                    if(term%3!=2){assigned=term;break;}
                if(assigned!=terms.size())
                    plan.warnings.push_back("No confirmed offering term is available for "+code+
                        "; it is placed in the earliest remaining regular semester for review.");
            }
        }
        PlanCourse item;
        item.course=course;
        item.prerequisite_statuses=prerequisiteStatuses(course);
        if(inProgress)item.requirement="In progress";
        else if(majorRequired.count(code))
            item.requirement=req.illustrative
                ?"Catalogue sample only; not a degree requirement"
                :"Degree Works major requirement";
        else if(required.count(code))
            item.requirement=req.illustrative
                ?"Catalogue sample only; not a degree requirement"
                :"Degree Works required";
        else if(chosen.count(code)){
            item.requirement=req.illustrative
                ?"Catalogue sample only; not a degree requirement"
                :"Elective selection";
            item.elective_selection=true;
        }
        else item.requirement="Prerequisite";
        item.counts_toward_degree=!nonDegree.count(code);
        item.first_term_credits=firstTermCredits;
        if(assigned==terms.size()){
            item.year_semester="Manual placement (no remaining regular semester)";
            item.manual_placement=true;
            item.warning=true;
            item.warnings.push_back("No regular semester remains after the scheduled prerequisites; arrange this course manually.");
            if(!course.credits_known)
                item.warnings.push_back("Credit units are missing; 3 credits are assumed.");
            manualPlacementCodes.insert(code);
            courseWarnings=true;
            plan.courses.push_back(std::move(item));
            continue;
        }
        item.warning=course.prereq_status==PrereqStatus::UNSCHEDULED||
                     course.prereq_status==PrereqStatus::UNKNOWN;
        if(item.warning)item.warnings.push_back("Prerequisites require review.");
        if(course.semesters.empty()&&!inProgress){
            assumedOfferingTerms=true;
            item.warnings.push_back("Catalogue offering information is unconfirmed; Semester A or Semester B is assumed. Confirm with the official catalogue.");
        }else if(course.semesters.empty()){
            item.warnings.push_back("Offering terms are missing; this placement follows the Degree Works In-progress list.");
        }
        else if(!hasSchedulableOffering(course))
            item.warnings.push_back("The catalogue lists no offering term; this placement requires confirmation.");
        if(std::find(course.semesters.begin(),course.semesters.end(),"Not offered")!=
               course.semesters.end()||
           std::find(course.semesters.begin(),course.semesters.end(),"X")!=
               course.semesters.end())
            item.warnings.push_back(
                "The catalogue has no confirmed Semester A/B offering; Semester A or Semester B is provisionally assumed. Confirm availability.");
        if(project&&static_cast<int>(assigned/3)!=plan.plan_years-1&&!inProgress)
            item.warnings.push_back("Project courses must be completed in the final year; check this placement.");
        if(project&&assigned<earliest)
            item.warnings.push_back("The final-year Project placement precedes a scheduled prerequisite; verify the sequence.");
        if(inProgress&&internship)
            item.warnings.push_back("In-progress course remains in Year 1 Semester A; confirm any Summer requirement.");
        if(inProgress&&project)
            item.warnings.push_back("This in-progress project is shown in Year 1 Semester A as listed in Degree Works; verify its final-year placement.");
        if(!inProgress&&conflictsWithEnglishCourse(code,assigned))
            item.warnings.push_back("This course shares a semester with LC0200A or LC0200B; verify the English-course separation requirement.");
        if(assigned<earliest)
            item.warnings.push_back("This course is placed before a scheduled prerequisite; verify the sequence.");
        if(termCredits[assigned]+firstTermCredits>creditLimit){
            item.warnings.push_back("This placement exceeds the configured per-semester credit limit.");
            plan.warnings.push_back(code+
                " is placed in a semester above the configured credit limit.");
        }
        if(!item.warnings.empty()){
            item.warning=true;
            courseWarnings=true;
        }
        item.year_semester=terms[assigned];
        termCredits[assigned]+=firstTermCredits;
        if(inProgress&&termCredits[assigned]>creditLimit)
            plan.warnings.push_back("Current enrollment in "+item.year_semester+
                " exceeds the configured per-semester credit limit.");
        if(secondTermCredits>0){
            item.second_year_semester=terms[assigned+1];
            item.second_term_credits=secondTermCredits;
            termCredits[assigned+1]+=secondTermCredits;
        }
        scheduledTerm[code]=assigned+(secondTermCredits>0?1:0);
        plan.courses.push_back(std::move(item));
    }
    if(assumedOfferingTerms)
        plan.warnings.push_back(
            "Some catalogue offering information is unconfirmed; those courses are provisionally placed in Semester A or Semester B. Confirm before registration.");
    if(courseWarnings)
        plan.warnings.push_back(
            "One or more courses have placement or prerequisite warnings; review the notes in each semester box.");
    if(plan.audit_remaining_credits>0) {
        int scheduledDegreeCredits=0;
        for(const auto&course:plan.courses)
            if(!course.manual_placement&&course.counts_toward_degree)
                scheduledDegreeCredits+=course.first_term_credits+
                    course.second_term_credits;
        plan.unplanned_audit_credits=std::max(
            0,plan.audit_remaining_credits-scheduledDegreeCredits);
        if(plan.unplanned_audit_credits>0)
            plan.warnings.push_back(
                std::to_string(plan.unplanned_audit_credits)+
                " Degree Works credits are not represented by the listed or selected course requirements. Check unlisted major, stream, or programme requirements before relying on this schedule.");
    }
    plan.warnings.insert(plan.warnings.end(),plan.issues.begin(),plan.issues.end());
    return plan;
}
