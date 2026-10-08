// Vulkanstorm development diagnostic; scripted UI fixtures are opt-in.
// Normal XUI/login/world initialization remains outside this checkpoint.
#pragma once
#include <memory>
#include <string>

class VSVulkanDiagnostic
{
public:
    explicit VSVulkanDiagnostic(const std::string& evidence_directory);
    ~VSVulkanDiagnostic();
    void initialize();
    bool frame();
    void cleanup();
    int exitCode() const;
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
