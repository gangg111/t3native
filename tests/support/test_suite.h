#pragma once
// remake/tests/support/test_suite.gd equivalent: expect / expect_equal / require (skip when an
// input is missing), test registration and the data folders the suites read.
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "gd/gd.h"

namespace t3test {

struct Paths {
    std::string imported;   // remake/imported (AssetCatalog.ROOT)
    std::string traces;     // work/traces (TraceFiles)
};
const Paths& paths();
std::string imported(const std::string& rel);
std::string trace(const std::string& name);

class Context {
public:
    bool expect(bool ok, const std::string& message = "");
    template <typename A, typename B>
    bool expect_equal(const A& actual, const B& expected, const std::string& message = "") {
        if (actual == expected) return expect(true);
        std::ostringstream s;
        s << message << ": " << show(actual) << " != " << show(expected);
        return expect(false, s.str());
    }
    // False (and the test counts as skipped) when the file is missing.
    bool require(const std::string& path, const std::string& hint = "");

    int failures = 0;
    bool skipped = false;
    std::vector<std::string> messages;

private:
    template <typename T>
    static std::string show(const T& v) {
        if constexpr (std::is_arithmetic_v<T>) return std::to_string(v);
        else if constexpr (std::is_convertible_v<T, std::string>) return std::string(v);
        else return "<value>";
    }
};

using TestFn = std::function<void(Context&)>;
struct Registrar {
    Registrar(const char* suite, const char* name, TestFn fn);
};

std::string show(const gd::PI32& v);

}  // namespace t3test

#define T3_TEST(suite, name)                                                          \
    static void suite##_##name(t3test::Context& t);                                   \
    static t3test::Registrar suite##_##name##_reg(#suite, #name, suite##_##name);     \
    static void suite##_##name(t3test::Context& t)
