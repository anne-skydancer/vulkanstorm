"""Compile the selected Vulkan device's real driver query and formatter."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class DriverVersionTests(unittest.TestCase):
    def test_windows_package_version_matches_vulkan_luid(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/vsvulkancontext.cpp').read_text()
        start = source.index('std::string windowsGraphicsDriverVersion(')
        helper = source[start:source.index('\n#endif', start)]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
using UINT=unsigned;
#define FAILED(hr) ((hr)<0)
#define __uuidof(type) 1
constexpr unsigned VK_API_VERSION_1_1=(1u<<22)|(1u<<12), VK_LUID_SIZE=8;
constexpr int VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES=1, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2=2;
struct VkPhysicalDeviceIDProperties {int sType=0;bool deviceLUIDValid=false;unsigned char deviceLUID[8]{};};
struct VkPhysicalDeviceProperties2 {int sType=0;void* pNext=nullptr;};
struct LARGE_INTEGER {int64_t QuadPart=0;};
struct DXGI_ADAPTER_DESC1 {uint64_t AdapterLuid=0;};
bool valid_luid=true,factory_failure=false,version_failure=false;
uint64_t selected_luid=73,reported_version=(32ull<<48)|(15ull<<16)|8088ull;
namespace Diligent {struct IRenderDeviceVk {
    unsigned version=VK_API_VERSION_1_1;
    unsigned GetVkVersion(){return version;}
    int GetVkPhysicalDevice(){return 73;}
};}
void vkGetPhysicalDeviceProperties2(int device,VkPhysicalDeviceProperties2* out){
    assert(device==73);auto& id=*static_cast<VkPhysicalDeviceIDProperties*>(out->pNext);
    id.deviceLUIDValid=valid_luid;std::memcpy(id.deviceLUID,&selected_luid,8);
}
struct IDXGIAdapter1 {
    uint64_t luid;int releases=0,version_queries=0;
    int GetDesc1(DXGI_ADAPTER_DESC1* out){out->AdapterLuid=luid;return 0;}
    int CheckInterfaceSupport(int,LARGE_INTEGER* out){++version_queries;out->QuadPart=reported_version;return version_failure?-1:0;}
    void Release(){++releases;}
} wrong{19},selected{73};
struct IDXGIFactory1 {
    int releases=0;
    int EnumAdapters1(unsigned i,IDXGIAdapter1** out){if(i>1)return -1;*out=i?&selected:&wrong;return 0;}
    void Release(){++releases;}
} factory;
int CreateDXGIFactory1(int,void** out){if(factory_failure)return -1;*out=&factory;return 0;}
namespace Microsoft {namespace WRL {template<class T>struct ComPtr {
    T* p=nullptr;~ComPtr(){if(p)p->Release();}
    T** GetAddressOf(){return &p;}T* operator->(){return p;}
};}}
std::string llformat(const char* format,...){char out[128];va_list args;va_start(args,format);
    vsnprintf(out,sizeof(out),format,args);va_end(args);return out;}
PRODUCTION_HELPER
int main(){
    Diligent::IRenderDeviceVk device;
    assert(windowsGraphicsDriverVersion(device)=="32.0.15.8088");
    assert(wrong.version_queries==0 && selected.version_queries==1);
    assert(wrong.releases==1 && selected.releases==1 && factory.releases==1);
    selected_luid=999;assert(windowsGraphicsDriverVersion(device).empty());
    assert(selected.version_queries==1);selected_luid=73;
    version_failure=true;assert(windowsGraphicsDriverVersion(device).empty());version_failure=false;
    factory_failure=true;assert(windowsGraphicsDriverVersion(device).empty());factory_failure=false;
    valid_luid=false;assert(windowsGraphicsDriverVersion(device).empty());valid_luid=true;
    reported_version=0;assert(windowsGraphicsDriverVersion(device).empty());
    device.version=1u<<22;assert(windowsGraphicsDriverVersion(device).empty());
}
'''.replace('PRODUCTION_HELPER', helper)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'windows_driver_version.cpp'
            path.write_text(fixture)
            exe = path.with_suffix('.exe')
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_selected_driver_metadata_and_vendor_fallbacks(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/vsvulkancontext.cpp').read_text()
        start = source.index('std::string graphicsDriverVersion(')
        helper = source[start:source.index('#if LL_WINDOWS\nstd::string windowsGraphicsDriverVersion', start)]
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>
#define VK_VERSION_MAJOR(v) ((v)>>22)
#define VK_VERSION_MINOR(v) (((v)>>12)&1023)
#define VK_VERSION_PATCH(v) ((v)&4095)
constexpr uint32_t VK_API_VERSION_1_1=(1u<<22)|(1u<<12), VK_API_VERSION_1_2=(1u<<22)|(2u<<12);
constexpr int VK_DRIVER_ID_NVIDIA_PROPRIETARY=4, VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS=5;
constexpr int VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES=1, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2=2;
constexpr int VK_SUCCESS=0;
constexpr const char* VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME="VK_KHR_driver_properties";
struct VkPhysicalDeviceProperties {uint32_t vendorID=0,driverVersion=0;};
struct VkPhysicalDeviceDriverProperties {int sType=0;int driverID=0;char driverInfo[64]{};};
struct VkPhysicalDeviceProperties2 {int sType=0;void* pNext=nullptr;};
struct VkExtensionProperties {char extensionName[64]{};};
namespace Diligent { struct IRenderDeviceVk {
    uint32_t version=VK_API_VERSION_1_2;
    uint32_t GetVkVersion(){return version;}
    int GetVkPhysicalDevice(){return 73;}
}; }
std::string llformat(const char* format, ...){char buffer[256];va_list args;va_start(args,format);
    vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);return buffer;}
int queries=0;bool extension_available=false;int enumeration_result=VK_SUCCESS;
int vkEnumerateDeviceExtensionProperties(int physical,const char*,uint32_t* count,VkExtensionProperties* out){
    assert(physical==73);
    if(out && extension_available)std::strcpy(out[0].extensionName,VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
    *count=extension_available?1:0;return enumeration_result;
}
void vkGetPhysicalDeviceProperties2(int physical,VkPhysicalDeviceProperties2* out){
    assert(physical==73 && out->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);++queries;
    auto& driver=*static_cast<VkPhysicalDeviceDriverProperties*>(out->pNext);
    assert(driver.sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
    driver.driverID=VK_DRIVER_ID_NVIDIA_PROPRIETARY;std::strcpy(driver.driverInfo,"580.88");
}
PRODUCTION_HELPERS
int main(){
    VkPhysicalDeviceProperties p{};VkPhysicalDeviceDriverProperties d{};
    p.vendorID=0x10de;p.driverVersion=(580u<<22)|(88u<<14)|(1u<<6)|2u;
    assert(graphicsDriverVersion(p,d)=="580.88.1.2");
    d.driverID=VK_DRIVER_ID_NVIDIA_PROPRIETARY;p.vendorID=123;
    assert(graphicsDriverVersion(p,d)=="580.88.1.2");
    std::strcpy(d.driverInfo,"580.88 (release build)");
    assert(graphicsDriverVersion(p,d)=="580.88 (release build)");
    // Bounded even if a broken driver forgets the terminator.
    std::fill(std::begin(d.driverInfo),std::end(d.driverInfo),'x');
    assert(graphicsDriverVersion(p,d)==std::string(64,'x'));
    d={};d.driverID=VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS;p.driverVersion=(101u<<14)|7085u;
    assert(graphicsDriverVersion(p,d)=="101.7085");
    d={};p.vendorID=0x8086;
#if LL_WINDOWS
    assert(graphicsDriverVersion(p,d)=="101.7085");
#else
    assert(graphicsDriverVersion(p,d)=="0.405.2989");
#endif
    p.vendorID=0x1002;p.driverVersion=(2u<<22)|(0u<<12)|355u;
    assert(graphicsDriverVersion(p,d)=="2.0.355");
    d.driverID=1;std::strcpy(d.driverInfo,"24.4.1 (AMD release)");
    assert(graphicsDriverVersion(p,d)=="24.4.1 (AMD release)");
    Diligent::IRenderDeviceVk device;
    assert(std::string(queryDriverProperties(device).driverInfo)=="580.88" && queries==1);
    device.version=VK_API_VERSION_1_1;
    assert(queryDriverProperties(device).driverInfo[0]==0 && queries==1);
    extension_available=true;
    assert(std::string(queryDriverProperties(device).driverInfo)=="580.88" && queries==2);
    enumeration_result=-1;
    assert(queryDriverProperties(device).driverInfo[0]==0 && queries==2);
    device.version=1u<<22;
    assert(queryDriverProperties(device).driverInfo[0]==0 && queries==2);
}
'''.replace('PRODUCTION_HELPERS', helper)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'driver_version.cpp'
            path.write_text(fixture)
            exe = path.with_suffix('.exe')
            for windows in (0, 1):
                with self.subTest(windows=windows):
                    built = subprocess.run([compiler, '-std=c++17', f'-DLL_WINDOWS={windows}', str(path), '-o', str(exe)], capture_output=True, text=True)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
