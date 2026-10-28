// Run the production XML overlay method without a viewer or graphics device.
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct LLXMLNode;
struct LLXMLNodePtr
{
    LLXMLNode* node = nullptr;
    LLXMLNode* operator->() const { return node; }
    explicit operator bool() const { return node != nullptr; }
    bool notNull() const { return node != nullptr; }
    bool isNull() const { return node == nullptr; }
    bool operator==(LLXMLNodePtr other) const { return node == other.node; }
};
struct LLStringTableEntry { std::string name; };
using LLXMLAttribList = std::map<const LLStringTableEntry*, LLXMLNodePtr>;

const LLStringTableEntry* entry(const std::string& name)
{
    static std::map<std::string, LLStringTableEntry> entries;
    return &entries.emplace(name, LLStringTableEntry{name}).first->second;
}
struct LLXMLNode
{
    std::string tag, mValue;
    LLXMLAttribList mAttributes;
    std::vector<LLXMLNodePtr> children;
    LLXMLNodePtr next;
    LLXMLNodePtr getFirstChild() { return children.empty() ? LLXMLNodePtr{} : children.front(); }
    LLXMLNodePtr getNextSibling() { return next; }
    void getAttribute(const LLStringTableEntry* key, LLXMLNodePtr& result, bool)
    {
        auto found = mAttributes.find(key);
        result = found == mAttributes.end() ? LLXMLNodePtr{} : found->second;
    }
    void getAttributeString(const char* key, std::string& value)
    {
        LLXMLNodePtr attribute;
        getAttribute(entry(key), attribute, false);
        value = attribute ? attribute->mValue : "";
    }
    static bool updateNode(LLXMLNodePtr&, LLXMLNodePtr&);
};
std::ostringstream diagnostics;
#define LL_WARNS() diagnostics
#define LL_ENDL std::endl

PRODUCTION_XML_UPDATE

LLXMLNodePtr allocate()
{
    static std::vector<std::unique_ptr<LLXMLNode>> nodes;
    nodes.push_back(std::make_unique<LLXMLNode>());
    return {nodes.back().get()};
}
std::string unhex(const std::string& text)
{
    std::string result;
    if (text == "_") return result;
    for (size_t i = 0; i < text.size(); i += 2)
        result += static_cast<char>(std::stoul(text.substr(i, 2), nullptr, 16));
    return result;
}
std::string hex(const std::string& text)
{
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : text)
    {
        result += digits[c >> 4];
        result += digits[c & 15];
    }
    return result.empty() ? "_" : result;
}
LLXMLNodePtr readNode()
{
    auto node = allocate();
    unsigned attributes, children;
    std::cin >> node->tag >> attributes >> children;
    for (unsigned i = 0; i < attributes; ++i)
    {
        std::string key, value;
        std::cin >> key >> value;
        auto attribute = allocate();
        attribute->mValue = unhex(value);
        node->mAttributes.emplace(entry(unhex(key)), attribute);
    }
    for (unsigned i = 0; i < children; ++i)
    {
        auto child = readNode();
        if (!node->children.empty()) node->children.back()->next = child;
        node->children.push_back(child);
    }
    return node;
}
void writeNode(LLXMLNodePtr node)
{
    std::cout << node->tag << ' ' << node->mAttributes.size() << ' ' << node->children.size() << '\n';
    for (auto& attribute : node->mAttributes)
        std::cout << hex(attribute.first->name) << ' ' << hex(attribute.second->mValue) << '\n';
    for (auto child : node->children) writeNode(child);
}
int main()
{
    unsigned cases;
    std::cin >> cases;
    while (cases--)
    {
        unsigned layers;
        std::cin >> layers;
        if (!layers) return 1;
        auto root = readNode();
        while (--layers)
        {
            auto patch = readNode();
            if (!LLXMLNode::updateNode(root, patch)) return 2;
        }
        writeNode(root);
    }
    return std::cin.fail() ? 3 : 0;
}
