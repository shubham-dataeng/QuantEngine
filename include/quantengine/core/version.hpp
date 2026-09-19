#pragma once

#include <string_view>

namespace quantengine::core {

[[nodiscard]] constexpr std::string_view get_version() noexcept {
    return "0.1.0";
}

[[nodiscard]] bool is_engine_ready() noexcept;

}  // namespace quantengine::core
