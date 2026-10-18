#include <cassert>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <vector>
#include "vsgputimingledger.h"
struct Owner {bool live=true;std::vector<float> values;std::function<void()> hook;bool current()const{return live;}void publish(float value)const{auto*self=const_cast<Owner*>(this);self->values.push_back(value);if(hook)hook();}};
struct Query {int id;};
int main(){
 auto a=std::make_shared<Owner>(),b=std::make_shared<Owner>();
 using Ledger=VSGPUTimingLedger<Owner,Query>;Ledger row;row.add(a,{0});row.add(a,{1});row.add(b,{2});
 int reads[5]{},recycles[5]{};bool ready[5]{false,true,true};
 auto read=[&](Query q){++reads[q.id];return VSGPUTimingResult{ready[q.id],true,q.id==0?1.25:2.5};};
 auto recycle=[&](Query q){++recycles[q.id];};
 assert(!row.poll(read,recycle));assert(a->values.empty());assert(b->values.size()==1&&b->values[0]==2.5f);assert(recycles[0]==0&&recycles[1]==1&&recycles[2]==1);
 ready[0]=true;assert(row.poll(read,recycle));assert(a->values.size()==1&&a->values[0]==3.75f);assert(reads[1]==1&&reads[2]==1);assert(row.poll(read,recycle));assert(a->values.size()==1&&recycles[0]==1);
 // A refused draw means an incomplete frame: never publish a partial sum.
 Ledger refused;refused.add(a,{3});refused.refuse(a);assert(refused.poll([](Query){return VSGPUTimingResult{true,true,99};},recycle));assert(a->values.size()==1&&recycles[3]==1);
 Ledger invalid;invalid.add(a,{4});assert(invalid.poll([](Query){return VSGPUTimingResult{true,false,0};},recycle));assert(a->values.size()==1&&recycles[4]==1);
 // Retirement before completion rejects old account/incarnation data.
 Ledger stale;stale.add(a,{0});a->live=false;assert(stale.poll([](Query){return VSGPUTimingResult{true,true,6};},[](Query){}));assert(a->values.size()==1);a->live=true;
 // Mark published before invoking callbacks, including reentrant polling.
 Ledger reentrant;reentrant.add(a,{0});a->hook=[&]{assert(reentrant.poll([](Query){assert(false);return VSGPUTimingResult{};},[](Query){assert(false);}));};assert(reentrant.poll([](Query){return VSGPUTimingResult{true,true,7};},[](Query){}));assert(a->values.size()==2&&a->values.back()==7);a->hook={};
 // Renderer drains frames in submission order even if newer GPU work is ready.
 std::deque<std::shared_ptr<Ledger>> frames;auto old=std::make_shared<Ledger>(),next=std::make_shared<Ledger>();old->add(a,{0});next->add(a,{1});frames.push_back(old);frames.push_back(next);bool oldReady=false;
 auto drain=[&]{while(!frames.empty()){if(!frames.front()->poll([&](Query q){return VSGPUTimingResult{q.id!=0||oldReady,true,q.id?9.:8.};},[](Query){}))break;frames.pop_front();}};
 drain();assert(frames.size()==2&&a->values.size()==2);oldReady=true;drain();assert(frames.empty()&&a->values.size()==4&&a->values[2]==8&&a->values[3]==9);
}
