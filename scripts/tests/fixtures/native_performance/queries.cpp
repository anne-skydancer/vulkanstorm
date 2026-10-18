#include <cassert>
#include <limits>
#include <vector>
#include <functional>
#include "vsgputimingledger.h"
#include "vsavatarperformance.h"
struct Owner {bool live=true;std::vector<float> values;std::function<void()> callback;bool current()const{return live;}void publish(float value)const{auto* self=const_cast<Owner*>(this);self->values.push_back(value);if(callback)callback();}};
int main(){
 auto avatar=std::make_shared<Owner>(),attachment=std::make_shared<Owner>(),retired=std::make_shared<Owner>();
 VSGPUTimingLedger<Owner,int> ledger;ledger.addShared({avatar,attachment,avatar,nullptr},7);ledger.add(retired,8);retired->live=false;
 unsigned reads=0,recycles=0;bool complete=false;
 auto read=[&](int){++reads;return VSGPUTimingResult{complete,true,2.5};};auto recycle=[&](int){++recycles;};
 assert(!ledger.poll(read,recycle));assert(reads==2&&recycles==0); // one read per physical query per nonblocking poll
 complete=true;assert(ledger.poll(read,recycle));assert(reads==4&&recycles==2);assert(avatar->values==std::vector<float>{2.5});assert(attachment->values==avatar->values&&retired->values.empty());
 assert(ledger.poll(read,recycle)&&reads==4&&recycles==2);
 VSGPUTimingLedger<Owner,int> invalid;invalid.addShared({avatar,attachment},9);invalid.refuse(avatar);unsigned invalidReads=0;
 assert(invalid.poll([&](int){++invalidReads;return VSGPUTimingResult{true,false,0};},recycle));assert(invalidReads==1&&avatar->values.size()==1&&attachment->values.size()==1);
 VSGPUTimingLedger<Owner,int> reentrant;avatar->callback=[&]{attachment->live=false;};reentrant.addShared({avatar,attachment},10);
 assert(reentrant.poll([](int){return VSGPUTimingResult{true,true,1};},recycle));assert(avatar->values.size()==2&&attachment->values.size()<=2);
 struct Row {std::optional<float> tuningMilliseconds;bool tooSlow=false;};std::vector<Row> rows;
 auto s=VSAvatarPerformance::summarize(rows);assert(!s.total&&!s.maximum&&!s.average&&s.owners==0);
 rows={{std::nullopt,false},{std::numeric_limits<float>::quiet_NaN(),false},{-1,false}};s=VSAvatarPerformance::summarize(rows);assert(s.owners==3&&!s.total&&s.sampled==0);
 rows.push_back({0,false});s=VSAvatarPerformance::summarize(rows);assert(s.sampled==1&&s.total==0&&s.average==0&&s.maximum==0);
 rows.push_back({4,true});rows.push_back({2,false});s=VSAvatarPerformance::summarize(rows);assert(s.owners==6&&s.sampled==3&&s.total==6&&s.average==1&&s.maximum==4);
 rows={{3,true}};s=VSAvatarPerformance::summarize(rows);assert(s.total==3&&!s.average&&s.averageCount==0);
}
