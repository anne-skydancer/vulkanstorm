#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <vector>
int epoch=1;
auto vs_native_im_guard(){int current=epoch;return [current]{return epoch==current;};}
struct LLRemoteParcelInfoObserver;
template<class T> struct LLHandle {std::weak_ptr<T> owner;bool isDead()const{return owner.expired();}T* get()const{return owner.lock().get();}};
struct Parcel {int parcel_id=3;};
struct LLRemoteParcelInfoObserver {std::function<void()> callback;void processParcelInfo(const Parcel&){callback();}};
using observer_multimap_t=std::multimap<int,LLHandle<LLRemoteParcelInfoObserver>>;
int published=0;
void deliver(observer_multimap_t& observers,Parcel parcel_data){ OBSERVERS ++published; }
int main(){
 observer_multimap_t observers;int a=0,b=0;
 auto first=std::make_shared<LLRemoteParcelInfoObserver>(),second=std::make_shared<LLRemoteParcelInfoObserver>();
 first->callback=[&]{++a;observers.clear();second.reset();};second->callback=[&]{++b;};
 observers.emplace(3,LLHandle<LLRemoteParcelInfoObserver>{first});observers.emplace(3,LLHandle<LLRemoteParcelInfoObserver>{second});
 deliver(observers,{});assert(a==1 && b==0 && published==1);
 first->callback=[&]{++a;++epoch;observers.clear();};second=std::make_shared<LLRemoteParcelInfoObserver>();second->callback=[&]{++b;};
 observers.emplace(3,LLHandle<LLRemoteParcelInfoObserver>{first});observers.emplace(3,LLHandle<LLRemoteParcelInfoObserver>{second});
 deliver(observers,{});assert(a==2 && b==0 && published==1);
 first.reset();second.reset();observers.emplace(3,LLHandle<LLRemoteParcelInfoObserver>{});deliver(observers,{});assert(observers.empty() && published==2);
}
