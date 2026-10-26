// Test runner: t3tests [--imported DIR] [--traces DIR] [filter]
// A filter is a substring of "suite.name". Exit code 1 when any test failed.
#include <chrono>
#include <cstdio>
#include <exception>

#include "dev/record_trace.h"
#include "gd/io.h"
#include "test_suite.h"

namespace t3test {

namespace {
struct Test {
    std::string suite, name;
    TestFn fn;
};
std::vector<Test>& registry() {
    static std::vector<Test> tests;
    return tests;
}
Paths g_paths{T3_WORKSPACE "/Tekken3Remake/remake/imported", T3_WORKSPACE "/Tekken3Remake/work/traces"};
}  // namespace

const Paths& paths() { return g_paths; }
std::string imported(const std::string& rel) { return gd::path_join(g_paths.imported, rel); }
std::string trace(const std::string& name) { return gd::path_join(g_paths.traces, name); }

Registrar::Registrar(const char* suite, const char* name, TestFn fn) { registry().push_back({suite, name, std::move(fn)}); }

bool Context::expect(bool ok, const std::string& message) {
    if (!ok) {
        failures++;
        messages.push_back(message.empty() ? "expectation failed" : message);
    }
    return ok;
}

bool Context::require(const std::string& path, const std::string& hint) {
    if (gd::file_exists(path)) return true;
    skipped = true;
    messages.push_back("missing " + path + (hint.empty() ? "" : " (" + hint + ")"));
    return false;
}

std::string show(const gd::PI32& v) {
    std::string s = "[";
    for (gd::Int i = 0; i < v.size(); ++i) s += (i ? ", " : "") + std::to_string(v[i]);
    return s + "]";
}

}  // namespace t3test

int main(int argc, char** argv) {
    using namespace t3test;
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--imported" && i + 1 < argc) g_paths.imported = argv[++i];
        else if (a == "--traces" && i + 1 < argc) g_paths.traces = argv[++i];
        else filter = a;
    }
    t3::TraceFiles::directory = g_paths.traces;
    int passed = 0, failed = 0, skipped = 0;
    for (const Test& test : registry()) {
        std::string full = test.suite + "." + test.name;
        if (!filter.empty() && full.find(filter) == std::string::npos) continue;
        Context ctx;
        auto start = std::chrono::steady_clock::now();
        try {
            test.fn(ctx);
        } catch (const std::exception& e) {
            ctx.failures++;
            ctx.messages.push_back(std::string("exception: ") + e.what());
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const char* status = ctx.failures ? "FAIL" : (ctx.skipped ? "SKIP" : "ok");
        std::printf("%-4s %s (%.0f ms)\n", status, full.c_str(), ms);
        for (const std::string& m : ctx.messages) std::printf("     %s\n", m.c_str());
        if (ctx.failures) failed++;
        else if (ctx.skipped) skipped++;
        else passed++;
    }
    std::printf("%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    return failed ? 1 : 0;
}
