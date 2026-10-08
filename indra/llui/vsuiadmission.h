// Explicit native UI construction admission, independent of graphics APIs. LGPL-2.1.
#pragma once
#include <functional>
#include <string_view>
#include <typeinfo>
#include <stdexcept>

class VSUIAdmission
{
public:
    using widget_gate=std::function<bool(const std::type_info&)>;
    using floater_gate=std::function<bool(std::string_view)>;
    VSUIAdmission(widget_gate widgets,floater_gate floaters,floater_gate panels={})
    {
        if (sWidgets || sFloaters || !widgets || !floaters)
            throw std::logic_error("Native UI admission requires exclusive closed-set ownership");
        sWidgets=std::move(widgets);sFloaters=std::move(floaters);sPanels=std::move(panels);
    }
    ~VSUIAdmission() { sWidgets={};sFloaters={};sPanels={}; }
    VSUIAdmission(const VSUIAdmission&)=delete;
    VSUIAdmission& operator=(const VSUIAdmission&)=delete;
    static bool widget(const std::type_info& type) { return !sWidgets || sWidgets(type); }
    static bool floater(std::string_view name) { return !sFloaters || sFloaters(name); }
    // A base LLPanel admission never implicitly admits a specialized factory.
    static bool panelFactory(std::string_view name) { return !sWidgets || (sPanels && sPanels(name)); }
private:
    inline static widget_gate sWidgets;
    inline static floater_gate sFloaters;
    inline static floater_gate sPanels;
};
