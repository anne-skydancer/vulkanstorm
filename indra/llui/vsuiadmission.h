// Explicit native UI construction admission, independent of graphics APIs. LGPL-2.1.
#pragma once
#include <functional>
#include <string_view>
#include <typeinfo>
#include <stdexcept>

class LLSD;

class VSUIAdmission
{
public:
    using widget_gate=std::function<bool(const std::type_info&)>;
    using child_gate=std::function<bool(std::string_view,std::string_view)>;
    using callback_gate=std::function<bool(std::string_view,const LLSD&)>;
    using floater_gate=std::function<bool(std::string_view)>;
    VSUIAdmission(widget_gate widgets,floater_gate floaters,floater_gate panels={},std::function<void(std::string_view)> unsupported={},floater_gate commands={},callback_gate callbacks={},child_gate children={})
    {
        if (sWidgets || sFloaters || !widgets || !floaters)
            throw std::logic_error("Native UI admission requires exclusive closed-set ownership");
        sWidgets=std::move(widgets);sFloaters=std::move(floaters);sPanels=std::move(panels);sUnsupported=std::move(unsupported);sCommands=std::move(commands);sCallbacks=std::move(callbacks);sChildren=std::move(children);
    }
    ~VSUIAdmission() { sWidgets={};sFloaters={};sPanels={};sUnsupported={};sCommands={};sCallbacks={};sChildren={}; }
    VSUIAdmission(const VSUIAdmission&)=delete;
    VSUIAdmission& operator=(const VSUIAdmission&)=delete;
    static bool child(std::string_view filename,std::string_view name) { return !sWidgets || !sChildren || sChildren(filename,name); }
    static bool callback(std::string_view name,const LLSD& parameter) { return !sWidgets || name.empty() || (sCallbacks && sCallbacks(name,parameter)); }
    static bool command(std::string_view name) { return !sWidgets || (sCommands && sCommands(name)); }
    static bool widget(const std::type_info& type) { return !sWidgets || sWidgets(type); }
    static bool floater(std::string_view name) { return !sFloaters || sFloaters(name); }
    // A base LLPanel admission never implicitly admits a specialized factory.
    static bool panelFactory(std::string_view name) { return !sWidgets || (sPanels && sPanels(name)); }
    static void unsupported(std::string_view name) { if (sUnsupported) sUnsupported(name); }
private:
    inline static widget_gate sWidgets;
    inline static floater_gate sFloaters;
    inline static floater_gate sPanels;
    inline static floater_gate sCommands;
    inline static callback_gate sCallbacks;
    inline static child_gate sChildren;
    inline static std::function<void(std::string_view)> sUnsupported;
};
