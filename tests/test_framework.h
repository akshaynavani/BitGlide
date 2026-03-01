#pragma once

// Tiny self-registering test framework so the project has no external test dependency.

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace bgtest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Failure {
    std::string message;
};

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline int run_all(const std::string& filter) {
    int passed = 0, failed = 0;
    for (const auto& t : registry()) {
        if (!filter.empty() && t.name.find(filter) == std::string::npos) continue;
        try {
            t.fn();
            ++passed;
            std::cout << "[ PASS ] " << t.name << "\n";
        } catch (const Failure& f) {
            ++failed;
            std::cout << "[ FAIL ] " << t.name << "\n         " << f.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[ FAIL ] " << t.name << "\n         unexpected exception: " << e.what() << "\n";
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace bgtest

#define BG_CAT_(a, b) a##b
#define BG_CAT(a, b) BG_CAT_(a, b)

#define TEST(name)                                                                  \
    static void BG_CAT(test_fn_, name)();                                           \
    static ::bgtest::Registrar BG_CAT(test_reg_, name)(#name, &BG_CAT(test_fn_, name)); \
    static void BG_CAT(test_fn_, name)()

#define BG_FAIL_AT(msg)                                                             \
    do {                                                                            \
        std::ostringstream os_;                                                     \
        os_ << __FILE__ << ":" << __LINE__ << ": " << msg;                          \
        throw ::bgtest::Failure{os_.str()};                                         \
    } while (0)

#define EXPECT_TRUE(cond) \
    do { if (!(cond)) BG_FAIL_AT("expected true: " #cond); } while (0)
#define EXPECT_FALSE(cond) \
    do { if (cond) BG_FAIL_AT("expected false: " #cond); } while (0)
#define EXPECT_EQ(a, b)                                                             \
    do {                                                                            \
        auto va_ = (a);                                                             \
        auto vb_ = (b);                                                             \
        if (!(va_ == vb_)) BG_FAIL_AT(#a " == " #b " (" << va_ << " vs " << vb_ << ")"); \
    } while (0)
#define EXPECT_NEAR(a, b, tol)                                                      \
    do {                                                                            \
        double va_ = (a), vb_ = (b);                                                \
        if (std::fabs(va_ - vb_) > (tol)) BG_FAIL_AT(#a " ~= " #b " (" << va_ << " vs " << vb_ << ")"); \
    } while (0)
#define EXPECT_THROW(stmt, ex)                                                      \
    do {                                                                            \
        bool thrown_ = false;                                                       \
        try { stmt; } catch (const ex&) { thrown_ = true; }                         \
        if (!thrown_) BG_FAIL_AT("expected " #ex " from " #stmt);                   \
    } while (0)
