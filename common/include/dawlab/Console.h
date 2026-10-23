#pragma once
// Switches the Windows console to UTF-8 so printf / std::cout output (including any non-ASCII
// characters) displays correctly; this is set up automatically when the program starts.
// Terminals on other platforms default to UTF-8, so nothing is needed there.

#if defined(_WIN32)
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int codePage);

namespace dawlab::detail {
inline const bool consoleIsUtf8 = (SetConsoleOutputCP(65001u) != 0);
} // namespace dawlab::detail
#endif

// By default, an assert in an MSVC Debug build (for example a vector index out of range) pops up a
// "Debug Assertion Failed" dialog and the program waits there for a button press, which hangs ctest
// and any automated run. Here the message is printed to stderr and the program exits instead
// (the test is reported as failed), with no dialog.
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#include <stdlib.h>

namespace dawlab::detail {
inline bool redirectCrtReportsToStderr() {
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);                          // the C assert()
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // abort() shows no dialog
    return true;
}
inline const bool crtReportsGoToStderr = redirectCrtReportsToStderr();
} // namespace dawlab::detail
#endif
