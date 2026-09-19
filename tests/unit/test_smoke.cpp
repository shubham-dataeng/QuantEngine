#include <gtest/gtest.h>

#include "quantengine/core/version.hpp"

namespace quantengine::test {

TEST(SmokeTest, EngineVersionIsNonEmpty) {
    constexpr auto version = quantengine::core::get_version();
    EXPECT_FALSE(version.empty());
    EXPECT_EQ(version, "0.1.0");
}

TEST(SmokeTest, EngineCoreIsReady) {
    EXPECT_TRUE(quantengine::core::is_engine_ready());
}

}  // namespace quantengine::test
