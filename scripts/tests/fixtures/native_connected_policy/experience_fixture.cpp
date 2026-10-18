#include <cassert>
#include <vector>
struct LLSD{int id=0;bool defined=false;bool isDefined()const{return defined;}LLSD operator[](const char*)const{return*this;}int asUUID()const{return id;}};
struct LLExperienceCache{inline static const char*EXPERIENCE_ID="id";};
struct LLFloaterReporter{inline static std::vector<int>reports;static void showFromExperience(int id){reports.push_back(id);}};
struct LLFloaterExperienceProfile{int mExperienceId=34;void onReportExperience();};
struct LLPanelExperienceLog{LLSD selected;LLSD getSelectedEvent(){return selected;}void onReportExperience();};
PROFILE
LOG
int main(){LLFloaterExperienceProfile profile;profile.onReportExperience();assert(LLFloaterReporter::reports==std::vector<int>({34}));LLPanelExperienceLog log;log.onReportExperience();assert(LLFloaterReporter::reports.size()==1);log.selected={71,true};log.onReportExperience();assert(LLFloaterReporter::reports==std::vector<int>({34,71}));}
