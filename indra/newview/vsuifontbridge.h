// Scoped connection of existing viewer font geometry to native packets. LGPL-2.1.
#pragma once
class VSUIResources;
class VSUIFontBridge
{
public:
    explicit VSUIFontBridge(VSUIResources&);
    ~VSUIFontBridge();
    VSUIFontBridge(const VSUIFontBridge&)=delete;
    VSUIFontBridge& operator=(const VSUIFontBridge&)=delete;
private:
    VSUIResources& mResources;
    bool mOwnsFontManager=false;
};
