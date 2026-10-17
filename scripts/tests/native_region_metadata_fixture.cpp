#include <map>
#include <string>
#include <vector>
#include <optional>
#include <cmath>
#include <limits>
#include <cassert>
using F32=float;using U64=unsigned long long;using S32=int;
struct LLSD {
 enum Type{Undefined,Map,Real,Integer,String,Binary} type=Undefined;double number=0;std::string text;std::map<std::string,LLSD> fields;std::vector<unsigned char> bytes;
 LLSD()=default;LLSD(double n):type(Real),number(n){}LLSD(int n):type(Integer),number(n){}LLSD(const char* s):type(String),text(s){}LLSD(std::string s):type(String),text(s){}
 bool has(const std::string& n)const{return fields.count(n);}bool isMap()const{return type==Map;}bool isReal()const{return type==Real;}bool isInteger()const{return type==Integer;}bool isBinary()const{return type==Binary;}bool isString()const{return type==String;}
 double asReal()const{return number;}int asInteger()const{return int(number);}std::string asString()const{return text;}std::vector<unsigned char> asBinary()const{return bytes;}
 LLSD& operator[](const std::string& n){type=Map;return fields[n];}const LLSD& operator[](const std::string& n)const{static const LLSD missing;auto i=fields.find(n);return i==fields.end()?missing:i->second;}
};
U64 ll_U64_from_sd(const LLSD& s){U64 n=0;for(auto b:s.bytes)n=(n<<8)|b;return n;}
struct VSNativeSession {METADATA};
PARSER
int main(){
 VSNativeSession::RegionMetadata initial;LLSD body;
 auto missing=nativeRegionMetadata(body,initial);assert(missing && !missing->billableKnown && missing->billableFactor==1 && !missing->productKnown && missing->productName=="unknown");
 body["billable_factor"]=0.0;auto zero=nativeRegionMetadata(body,initial);assert(zero && zero->billableKnown && zero->billableFactor==0);
 body["billable_factor"]=std::numeric_limits<double>::quiet_NaN();assert(!nativeRegionMetadata(body,initial));body["billable_factor"]=std::numeric_limits<double>::max();assert(!nativeRegionMetadata(body,initial));body["billable_factor"]="not-number";assert(!nativeRegionMetadata(body,initial));
 body["billable_factor"]=2.5;body["region_protocols"].type=LLSD::Binary;body["region_protocols"].bytes={128,0,0,0,0,0,0,1};
 body["product"]["cpu_class"]=7;body["product"]["cpu_ratio"]=22;body["product"]["colo"]="real-colo";body["product"]["sku"]="real-sku";body["product"]["name"]="real-product";
 auto actual=nativeRegionMetadata(body,initial);assert(actual && actual->protocols==0x8000000000000001ULL && actual->protocolsKnown && actual->productKnown && actual->billableKnown);assert(actual->productName=="real-product" && actual->cpuRatio==22 && actual->billableFactor==2.5);
 auto prior=*actual;LLSD absent;auto retained=nativeRegionMetadata(absent,prior);assert(retained && retained->productName=="real-product" && retained->protocolsKnown);
 body["product"]["name"]=std::string(256,'a');assert(!nativeRegionMetadata(body,prior));assert(prior.productName=="real-product");
 body["product"]["name"]="real-product";body["region_protocols"].bytes.pop_back();assert(!nativeRegionMetadata(body,prior));
}
