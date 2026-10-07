// Vulkanstorm development diagnostic; no UI/world initialization is admitted.
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
