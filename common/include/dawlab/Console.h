#pragma once
// 讓 Windows console 用 UTF-8 顯示 printf / std::cout 的繁體中文（程式一啟動就自動設定）。
// 其他平台的 terminal 預設就是 UTF-8，不需要做事。

#if defined(_WIN32)
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int codePage);

namespace dawlab::detail {
inline const bool consoleIsUtf8 = (SetConsoleOutputCP(65001u) != 0);
} // namespace dawlab::detail
#endif

// MSVC Debug 版的 assert（例如 vector 索引越界）預設會跳出「Debug Assertion Failed」對話框，
// 程式就停在那裡等人按按鈕，ctest 和自動化流程會整個卡住。
// 這裡改成把訊息印到 stderr、直接結束程式（測試會判定失敗），不跳視窗。
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
    _set_error_mode(_OUT_TO_STDERR);                          // C 的 assert()
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // abort() 不跳視窗
    return true;
}
inline const bool crtReportsGoToStderr = redirectCrtReportsToStderr();
} // namespace dawlab::detail
#endif
