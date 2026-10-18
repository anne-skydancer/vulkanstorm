#include <cassert>
#include <memory>
struct LLRect{int value=0;};
struct LLToolTip{static int count;struct Params{std::shared_ptr<int> callback;};LLToolTip(){++count;}~LLToolTip(){--count;}};int LLToolTip::count=0;
struct LLToolTipMgr{LLToolTip*mToolTip=nullptr;bool mNeedsToolTip=false,mToolTipsBlocked=false;LLToolTip::Params mLastToolTipParams,mNextToolTipParams;LLRect mMouseNearRect;void resetToolTips();};
METHOD
int main(){LLToolTipMgr owner;auto account=std::make_shared<int>(7);std::weak_ptr<int> weak=account;owner.mLastToolTipParams.callback=account;owner.mNextToolTipParams.callback=account;account.reset();owner.mToolTip=new LLToolTip;owner.mNeedsToolTip=true;owner.mToolTipsBlocked=true;owner.mMouseNearRect.value=1;assert(!weak.expired());owner.resetToolTips();assert(weak.expired());assert(LLToolTip::count==0);assert(!owner.mNeedsToolTip&&!owner.mToolTip&&!owner.mToolTipsBlocked&&owner.mMouseNearRect.value==0);owner.resetToolTips();assert(LLToolTip::count==0);owner.mToolTip=new LLToolTip;owner.resetToolTips();assert(LLToolTip::count==0);}
