#pragma once

#include <string_view>

namespace loggable {
enum class LogLevel { Error, Warning, Debug, Verbose };
class Loggable {
public:
  explicit Loggable(std::string_view) {}
};
template <typename... Args>
void log(Args&&...) {}
} // namespace loggable

#define LOG(...) loggable::log(__VA_ARGS__)
