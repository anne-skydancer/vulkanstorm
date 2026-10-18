#include <cassert>
#include <functional>
#include <string>
#include <vector>
struct LLSD{};
struct Field{std::string value;bool provided=false;bool isProvided()const{return provided;}const std::string& operator()()const{return value;}};
struct Callback{int slot=0;bool provided=false;Field control_name;bool isProvided()const{return provided;}std::string function_name()const{return std::to_string(slot);}LLSD parameter()const{return {};}};
bool nativeActive=true;unsigned allowed=7;
struct VSUIAdmission{static bool callback(const std::string& name,LLSD){return !nativeActive||(allowed&(1u<<std::stoi(name)));}};
struct LLControlVariable{bool value=true;void set(bool v){value=v;}}control;
template<class F>struct Signal{std::vector<F> callbacks;void connect(F f){callbacks.push_back(f);}unsigned num_slots(){return unsigned(callbacks.size());}template<class A>bool operator()(A* value,LLSD arg){bool result=true;for(auto& f:callbacks)result&=f(value,arg);return result;}};
struct LLUICtrl
{
 bool enabled=true,visible=true;unsigned commits=0;LLControlVariable* mEnabledControlVariable=nullptr;
 virtual void onCommit(){++commits;}void setEnabled(bool v){enabled=v;}void setVisible(bool v){visible=v;}
 template<class P>void initFromParams(const P&){enabled=true;}
 void setEnabledControlVariable(LLControlVariable* value){mEnabledControlVariable=value;}
 LLControlVariable* findControl(const std::string& name){return name=="known"?&control:nullptr;}
};
struct PieSlice:LLUICtrl
{
 struct Params{Callback on_click{0},on_visible{1},on_enable{2};};
 using Enable=std::function<bool(PieSlice*,LLSD)>;std::function<void()> click;
 Signal<Enable> mEnableSignal,mVisibleSignal;bool mCheckEnableOnce=false,mDoUpdateEnabled=true,mNativeCallbacksAllowed=true;
 void setCommitCallback(std::function<void()> value){click=std::move(value);}
 std::function<void()> initCommitCallback(const Callback&){return []{};}
 Enable initEnableCallback(const Callback& p){return [id=p.slot](PieSlice*,LLSD){return id!=2;};}
 void setEnableCallback(Enable cb){mEnableSignal.connect(std::move(cb));}
 void initFromParams(const Params&);void updateEnabled();void updateVisible();void onCommit()override;
};
METHODS
int main()
{
 for(unsigned active=0;active<2;++active)for(unsigned policy=0;policy<8;++policy)for(unsigned present=0;present<8;++present)for(unsigned known=0;known<3;++known)
 {
  nativeActive=active;allowed=policy;control.value=true;PieSlice slice;PieSlice::Params p;
  p.on_click.provided=present&1;p.on_visible.provided=present&2;p.on_enable.provided=present&4;
  p.on_enable.control_name={known==1?"known":"missing",known!=0};
  slice.initFromParams(p);const bool permit=!active||((present&~policy)==0);
  assert(slice.mNativeCallbacksAllowed==permit);
  assert(bool(slice.click)==bool((present&1)&&(!active||(policy&1))));
  assert(slice.mVisibleSignal.num_slots()==unsigned(bool((present&2)&&(!active||(policy&2)))));
  assert(slice.mEnableSignal.num_slots()==unsigned(bool((present&4)&&(!active||(policy&4)))));
  slice.onCommit();assert(slice.commits==unsigned(permit));
  if(!permit){assert(!slice.enabled);slice.enabled=true;slice.updateEnabled();assert(!slice.enabled);}
  else
  {
   slice.updateEnabled();slice.updateVisible();
   if(present&4)
   {
    if(known==1)assert(!control.value);else assert(!slice.enabled);
   }
  }
 }
}
