// Region storage/logging fixture; insertion and tracking methods are production.
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
using U16 = uint16_t;
using U32 = uint32_t;
using U64 = uint64_t;
using F64 = double;
[[maybe_unused]] constexpr U16 REGION_WIDTH_UNITS = 256;
struct LLUUID { std::string getString() const { return {}; } };
struct NullLog { template<class T> NullLog& operator<<(const T&) { return *this; } };
#define LL_DEBUGS(category) NullLog{}
#define LL_ENDL 0
U64 to_region_handle(U32 x, U32 y) { return (U64(x) << 32) | y; }
struct LLSimInfo
{
    U16 width = 0, height = 0;
    U32 access = 0;
    void setName(const std::string&) {}
    void setAccess(U32 value) { access = value; }
    void setRegionFlags(U32) {}
    void setLandForSaleImage(const LLUUID&) {}
    void setSize(U16 x, U16 y) { width = x; height = y; }
    bool isDown() const { return access == 254; }
};
struct LLWorldMap
{
    static LLWorldMap* instance;
    std::map<U64, LLSimInfo> regions;
    bool mIsTrackingLocation = true;
    double mTrackingLocation[2] = {0, 0};
    bool valid = false, invalid = false;
    static LLWorldMap* getInstance() { return instance; }
    LLSimInfo* simInfoFromHandle(U64 handle)
    {
        auto found = regions.find(handle);
        return found == regions.end() ? nullptr : &found->second;
    }
    LLSimInfo* createSimInfoFromHandle(U64 handle) { return &regions[handle]; }
    void setTrackingInvalid() { invalid = true; }
    void setTrackingValid() { valid = true; }
    static bool insertRegion(U32, U32, U16, U16, std::string&, LLUUID&, U32, U64);
    bool isTrackingInRectangle(F64, F64, F64, F64);
};
LLWorldMap* LLWorldMap::instance = nullptr;

enum { _PREHASH_Size, _PREHASH_SizeX, _PREHASH_SizeY };
struct Message
{
    U16 x, y;
    bool has_size = true;
    int getNumberOfBlocksFast(int) const { return has_size ? 1 : 0; }
    void getU16Fast(int, int field, U16& value, int) const
    {
        value = field == _PREHASH_SizeX ? x : y;
    }
};
void decodeViewer(Message*, U16&, U16&);
void decodeFS(Message*, U16&, U16&);

// PRODUCTION_METHODS

int main()
{
    struct Case { U16 x, y, expected_x, expected_y; };
    const Case cases[] = {{0,0,256,256}, {512,0,512,256}, {0,1024,256,1024},
                          {256,256,256,256}, {512,1024,512,1024}};
    for (auto decoder : {decodeViewer, decodeFS})
    {
        Message no_size{0, 0, false};
        U16 x = 256, y = 256;
        decoder(&no_size, x, y);
        assert(x == 256 && y == 256);
        Message bad_width{17, 1024};
        decoder(&bad_width, x, y);
        assert(x == 256 && y == 1024);
        Message bad_height{512, 17};
        decoder(&bad_height, x, y);
        assert(x == 512 && y == 256);
    }
    for (int route = 0; route < 3; ++route)
    for (const auto& c : cases) for (U32 access : {0u, 254u, 255u})
    {
        // Points cover the interior and both exclusive upper boundaries.
        for (int point = 0; point < 3; ++point)
        {
            LLWorldMap map;
            LLWorldMap::instance = &map;
            map.mTrackingLocation[0] = 4096 + c.expected_x - (point == 1 ? 0 : 1);
            map.mTrackingLocation[1] = 8192 + c.expected_y - (point == 2 ? 0 : 1);
            std::string name = "region";
            LLUUID image;
            U16 x = c.x, y = c.y;
            Message reply{x, y};
            if (route == 1) decodeViewer(&reply, x, y);
            if (route == 2) decodeFS(&reply, x, y);
            bool inserted = LLWorldMap::insertRegion(4096,8192,x,y,name,image,access,0);
            assert(inserted == (access != 255));
            assert(map.invalid == (point == 0 && access >= 254));
            assert(map.valid == (point == 0 && access < 254));
            if (inserted)
            {
                auto* sim = map.simInfoFromHandle(to_region_handle(4096,8192));
                assert(sim->width == c.expected_x && sim->height == c.expected_y);
            }
        }
    }
}
