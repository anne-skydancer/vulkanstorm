#include <cassert>
#include <string>
#include <sstream>
#include <limits>
using F32=float;using S32=int;constexpr F32 F32_MAX=std::numeric_limits<float>::max();
CONSTANTS
struct Grid{bool open=false;static inline bool available=true;static Grid& instance(){static Grid v;return v;}static Grid* getInstance(){return available?&instance():nullptr;}bool isInOpenSim()const{return open;}};using LLGridManager=Grid;
struct Settings{float getF32(const char*){return 17;}}gSavedSettings;
std::string SYSTEM_FROM;std::ostringstream logstream;
#define LL_DEBUGS(...) logstream
#define LL_ENDL '\n'
HEADER
METHODS
int main(){
 Grid::available=false;SYSTEM_FROM="unchanged";VSRegionLimits model;assert(model.getRegionMaxHeight()==SL_MAX_OBJECT_Z && model.getAllowMinimap() && SYSTEM_FROM=="unchanged");Grid::available=true;model.refreshLimits();assert(model.getAllowMinimap() && model.getAllowPhysicalPrims() && model.getAllowRenderWater());
 assert(model.getMaxLinkedPrims()==MAX_CHILDREN_PER_TASK && model.getMaxPhysLinkedPrims()==MAX_CHILDREN_PER_PHYSICAL_TASK);
 assert(model.getRegionMaxPrimScale()==SL_DEFAULT_MAX_PRIM_SCALE && model.getRegionMaxHeight()==SL_MAX_OBJECT_Z);
 model.setAllowMinimap(false);model.setMaxLinkedPrims(7);model.setMaxDragDistance(-1);assert(!model.getAllowMinimap() && model.getMaxLinkedPrims()==7 && model.getMaxDragDistance()==17);
 model.refreshLimits();assert(model.getAllowMinimap() && model.getMaxLinkedPrims()==MAX_CHILDREN_PER_TASK);
 model.setMaxLinkedPrims(-1);assert(model.getMaxLinkedPrims()==10000);model.setMaxInventoryItemsTransfer(-1);assert(model.getMaxInventoryItemsTransfer()==42);
 model.setRegionMaxHeight(-1);assert(model.getRegionMaxHeight()==OS_MAX_OBJECT_Z && model.getMaxPrimZPos()==OS_MAX_OBJECT_Z);
 model.setDrawDistance(-3);assert(model.getDrawDistance()==-1);model.setDrawDistance(123);assert(model.getDrawDistance()==123);
 model.setTerrainDetailScale(-2);assert(model.getTerrainDetailScale()==-1);
 model.setMinPrimXPos(-1);assert(model.getMinPrimXPos()==0);
 Grid::instance().open=true;model.refreshLimits();
#ifdef OPENSIM
 assert(model.getRegionMaxPrimScale()==OS_DEFAULT_MAX_PRIM_SCALE && model.getRegionMaxHeight()==OS_MAX_OBJECT_Z && SYSTEM_FROM=="Grid");
 model.setMinPrimZPos(OS_MIN_OBJECT_Z-1);assert(model.getMinPrimZPos()==OS_MIN_OBJECT_Z);
#else
 assert(model.getRegionMaxPrimScale()==SL_DEFAULT_MAX_PRIM_SCALE && SYSTEM_FROM=="Second Life");
#endif
 Grid::instance().open=false;model.refreshLimits();assert(model.getRegionMaxPrimScale()==SL_DEFAULT_MAX_PRIM_SCALE && model.getMaxLinkedPrims()==MAX_CHILDREN_PER_TASK);
}
