// SDK_A smoke test — verifies the dumped aggregator header parses cleanly.
// Catches regressions in the cross-package dep tracker (e.g. value-holding
// wrappers like TMap/TSet/TPair landing in fwdDeps and producing "field has
// incomplete type" errors at instantiation).
//
// Lives outside misc/UEDump3r/ so re-dumps don't clobber it. The
// __has_include guard means a fresh clone without a dump still compiles
// clean (the TU is just empty).
//
// Run from repo root:
//   clang++ -std=c++20 -fsyntax-only \
//       -I misc/UEDump3r/com.tencent.tmgp.dfm/SDK_A \
//       misc/sdk_smoke/Demo.cpp

#if __has_include("../UEDump3r/com.tencent.tmgp.dfm/SDK_A/SDK.hpp")
#include "../UEDump3r/com.tencent.tmgp.dfm/SDK_A/SDK.hpp"
#endif
