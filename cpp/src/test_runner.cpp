#include "test_runner.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Test {

static std::vector<std::pair<std::string, Fn>>& registry() {
    static std::vector<std::pair<std::string, Fn>> r;
    return r;
}

void add(const char* name, Fn fn) { registry().emplace_back(name, fn); }

int runAll() {
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS) != 0 || GetLastError() == ERROR_ACCESS_DENIED) {
        (void)freopen("CONOUT$", "w", stderr);
        (void)freopen("CONOUT$", "w", stdout);
    }
#endif
    const char* logPath = "selftest.log";
#ifdef _WIN32
    char pathBuf[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, pathBuf, MAX_PATH) > 0) {
        for (int i = static_cast<int>(std::strlen(pathBuf)) - 1; i >= 0; --i) {
            if (pathBuf[i] == '\\' || pathBuf[i] == '/') {
                std::strcpy(pathBuf + i + 1, "selftest.log");
                logPath = pathBuf;
                break;
            }
        }
    }
#endif
    FILE* log = std::fopen(logPath, "w");
    const size_t nTests = registry().size();
    if (log) {
        std::fprintf(log, "%zu tests\n", nTests);
        std::fflush(log);
    }
    int failed = 0;
    for (const auto& item : registry()) {
        std::fprintf(stderr, "test %s ... ", item.first.c_str());
        if (log) std::fprintf(log, "test %s ... ", item.first.c_str());
        if (item.second()) {
            std::fprintf(stderr, "ok\n");
            if (log) {
                std::fprintf(log, "ok\n");
                std::fflush(log);
            }
        } else {
            std::fprintf(stderr, "FAIL\n");
            if (log) {
                std::fprintf(log, "FAIL\n");
                std::fflush(log);
            }
            ++failed;
        }
    }
    std::fprintf(stderr, "%zu tests, %d failed\n", registry().size(), failed);
    if (log) {
        std::fprintf(log, "%zu tests, %d failed\n", registry().size(), failed);
        std::fclose(log);
    }
    return failed ? 1 : 0;
}

} // namespace Test