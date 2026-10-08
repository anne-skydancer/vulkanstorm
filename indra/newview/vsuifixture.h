// Existing XUI and input qualification inside the native viewer. LGPL-2.1.
#pragma once
#include "vsuirenderer.h"
#include <memory>
class VSUIResources;
class LLWindow;
class VSUIFixture
{
  public:
    VSUIFixture(VSUIResources &, LLWindow *);
    ~VSUIFixture();
    VSUIFixture(const VSUIFixture &) = delete;
    VSUIFixture &operator=(const VSUIFixture &) = delete;
    std::vector<VSUIRenderer::Packet> draw();
    bool unicode(unsigned character);
    bool key(unsigned char key, unsigned mask);
    void verifyInput();
    void focus(bool value);
    bool focused() const;
    std::string inputText() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
