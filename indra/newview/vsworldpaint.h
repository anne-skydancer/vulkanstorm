// Resource-backed painter for genuine shared world/tool immediate geometry.
// LGPL-2.1, like the viewer.
#pragma once
#include "vsimmediategeometry.h"
#include "vsworldrenderer.h"
#include "vsresourcebudget.h"
class VSWorldPaint
{
public:
    using Reserve=std::function<std::shared_ptr<const VSResourceBudget::Reservation>(std::size_t)>;
    explicit VSWorldPaint(VSWorldRenderer&,Reserve,std::function<bool()> current);
    ~VSWorldPaint();
    VSImmediateGeometry& begin();
    // Capture the real reference matrix baseline while retaining agent-local
    // mesh vertices. Camera motion updates only immutable draw projection.
    VSImmediateGeometry& begin(const glm::mat4& modelview,const glm::mat4& projection);
    bool finish(std::vector<VSWorldRenderer::Packet>&);
    void retire();
private:
    struct Impl;std::unique_ptr<Impl>mImpl;
};
