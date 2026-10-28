// SPDX-License-Identifier: LGPL-2.1-only
// Standalone regression tests for the production VVM version parser.
#include "llversionparser.h"
#include <cassert>
#include <string_view>
int main()
{
    using namespace LLViewerVersion;
    const Components running{1, 0, 0, 99999};
    assert(compare(running, "1.0.0-canary.100000") == -1);
    assert(compare(running, "1.0.0-canary.99999") == 0);
    assert(compare(running, "1.0.0-canary.99998") == 1);
    assert(compare(running, "1.0.0.100000") == -1);
    assert(compare(running, "1.0.0.99999") == 0);
    assert(compare(running, "1.0.0.99998") == 1);
    assert(compare(running, "2.0.0-canary.1") == -1);
    assert(compare(running, "0.9.9.999999") == 1);
    for (std::string_view invalid : {"", "1.0.0", "1.0.0-canary", "1.0.0-canary-100000",
         "1.0.0-canary.100000garbage", "1.0.0.123.4", "-1.0.0.123", "1.0.0.+123",
         "1.0.0-beta.123", "1.0.0.18446744073709551616", " 1.0.0.123"})
        assert(!compare(running, invalid));
}
