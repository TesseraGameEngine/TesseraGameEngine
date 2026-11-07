#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <chrono>

namespace TestFramework {

struct TestCase {
    std::string suiteName;
    std::string testName;
    std::function<void()> func;
};

class TestRegistry {
public:
    static TestRegistry& Get() {
        static TestRegistry instance;
        return instance;
    }

    void Register(const std::string& suite, const std::string& name, std::function<void()> func) {
        m_tests.push_back({suite, name, func});
    }

    int RunAll() {
        int passed = 0;
        int failed = 0;
        std::cout << "===================================================\n";
        std::cout << "  RUNNING ENGINE UNIT TEST SUITE (" << m_tests.size() << " tests)\n";
        std::cout << "===================================================\n\n";

        auto startTime = std::chrono::high_resolution_clock::now();

        std::string currentSuite = "";
        for (const auto& test : m_tests) {
            if (test.suiteName != currentSuite) {
                currentSuite = test.suiteName;
                std::cout << "\n[" << currentSuite << "]\n";
            }

            try {
                test.func();
                std::cout << "  [PASS] " << test.testName << "\n";
                passed++;
            } catch (const std::exception& e) {
                std::cout << "  [FAIL] " << test.testName << " -> " << e.what() << "\n";
                failed++;
            } catch (...) {
                std::cout << "  [FAIL] " << test.testName << " -> Unknown exception thrown\n";
                failed++;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        double durationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        std::cout << "\n===================================================\n";
        std::cout << "  RESULTS: " << passed << " PASSED, " << failed << " FAILED (" << durationMs << " ms)\n";
        std::cout << "===================================================\n";

        return failed == 0 ? 0 : 1;
    }

private:
    std::vector<TestCase> m_tests;
};

struct AutoRegister {
    AutoRegister(const std::string& suite, const std::string& name, std::function<void()> func) {
        TestRegistry::Get().Register(suite, name, func);
    }
};

} // namespace TestFramework

#define TEST_CASE(suite, name) \
    static void test_##suite##_##name(); \
    static TestFramework::AutoRegister reg_##suite##_##name(#suite, #name, test_##suite##_##name); \
    static void test_##suite##_##name()

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #condition + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

#define ASSERT_FALSE(condition) \
    ASSERT_TRUE(!(condition))

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #a + " != " + #b + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

#define ASSERT_FLOAT_NEAR(a, b, eps) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { \
            throw std::runtime_error(std::string("Assertion failed: |") + #a + " - " + #b + "| <= " + #eps + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

