#include <cassert>
#include <type_traits>
#include <initializer_list>
using S32=int;
int gSavedSettings=0,requested=1;bool showTitles=true;
template<class T>struct LLCachedControl {LLCachedControl(int,const char*,T){}operator T()const {if constexpr(std::is_same_v<T,bool>)return showTitles;else return requested;}};
struct LLVOAvatar {static inline S32 sRenderName=0;static inline bool sRenderGroupTitles=false;};
struct VSAvatarNameTag {static void refreshPreferences(S32);};
CURRENT
int main(){for(int mode=0;mode<3;++mode)for(int simulator=0;simulator<3;++simulator)for(bool titles:{false,true}){requested=mode;showTitles=titles;VSAvatarNameTag::refreshPreferences(simulator);const int reference=mode>simulator?simulator:mode;assert(LLVOAvatar::sRenderName==reference);assert(LLVOAvatar::sRenderGroupTitles==(titles&&reference>0));}}
