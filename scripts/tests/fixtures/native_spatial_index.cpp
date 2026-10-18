#include "vsspatialindex.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <random>
#include <set>
#include <iostream>
using Bounds=VSSpatialIndex::Bounds;
bool overlaps(const Bounds&a,const Bounds&b){for(unsigned i=0;i<3;++i)if(a[1][i]<b[0][i]||b[1][i]<a[0][i])return false;return true;}
int main(){VSResourceBudget budget(64*1024*1024);bool refuse=false;auto reserve=[&](std::size_t n){return refuse?std::shared_ptr<const VSResourceBudget::Reservation>():budget.reserve(n);};std::map<unsigned,Bounds> source;
 {
 VSSpatialIndex index(reserve);std::mt19937 random(1845);auto bounds=[&]{Bounds b;for(unsigned a=0;a<3;++a){b[0][a]=float(random()%10000)*.1f-500;b[1][a]=b[0][a]+float(random()%80)*.1f;}return b;};
 for(unsigned i=0;i<10000;++i){auto b=bounds();source[i]=b;assert(index.upsert(i,b));}assert(index.size()==source.size());const auto unchanged=budget.used();for(unsigned i=0;i<10000;++i)assert(index.upsert(i,source[i]));assert(budget.used()==unchanged);
 for(unsigned i=0;i<200;++i){auto b=bounds();for(unsigned a=0;a<3;++a)b[1][a]+=70;auto q=index.query(b);assert(q);std::set<unsigned>got(q->keys.begin(),q->keys.end()),expected;for(auto&[key,row]:source)if(overlaps(row,b))expected.insert(key);assert(got==expected);}
 for(unsigned i=0;i<5000;++i){auto b=bounds();source[i]=b;assert(index.upsert(i,b));}for(unsigned i=5000;i<10000;++i){source.erase(i);assert(index.erase(i));}assert(index.size()==source.size());
 auto q=index.query({{{-1000,-1000,-1000},{1000,1000,1000}}});assert(q&&q->keys.size()==5000);const auto retained=q->residency;index.clear();assert(index.valid()&&index.size()==0);assert(budget.used()>=retained->bytes());
 for(auto&[key,row]:source)assert(index.upsert(key,row));refuse=true;assert(!index.query({{{-1000,-1000,-1000},{1000,1000,1000}}}));assert(index.valid());assert(!index.upsert(15001,{{{0,0,0},{1,1,1}}}));assert(!index.valid());assert(!index.query({{{-1000,-1000,-1000},{1000,1000,1000}}}));assert(source.size()==5000);refuse=false;index.clear();for(auto&[key,row]:source)assert(index.upsert(key,row));assert(index.valid()&&index.size()==5000);
 }
 assert(budget.used()==0);std::cout<<"10000 sources, 200 exact queries, moving/removing half, refusal/rebuild and budget retirement pass\n";
}
