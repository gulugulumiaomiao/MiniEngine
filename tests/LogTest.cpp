#include "core/logging/Log.h"

#include <gtest/gtest.h>

#include <cstdlib>

TEST(LogTest, LevelsWriteWithoutCrashing) {
    EXPECT_NO_FATAL_FAILURE(engine::Log::info("LogTest", "Info message"));
    EXPECT_NO_FATAL_FAILURE(
        engine::Log::debug("LogTest", "Object count: %u, name: %s", 2U, "Triangle"));
    EXPECT_NO_FATAL_FAILURE(engine::Log::warn("LogTest", "Warn message"));
    EXPECT_NO_FATAL_FAILURE(engine::Log::error("LogTest", "Result: %d, time: %.2f ms", -1, 1.25));
}

// Log::fatal is [[noreturn]]: it writes the fatal record to stderr and exits with
// EXIT_FAILURE. The death-test child reruns this binary, so a single assertion
// covers both the exit code and the message reaching stderr -- the old WILL_FAIL
// ctest entry could only observe "some nonzero exit". A bare string matcher is
// interpreted by gtest as a regex over stderr (HasSubstr belongs to gmock).
TEST(LogDeathTest, FatalExitsWithFailureCode) {
    EXPECT_EXIT(engine::Log::fatal("LogTest", "Fatal exits the process"),
                ::testing::ExitedWithCode(EXIT_FAILURE),
                "Fatal exits the process");
}
