#pragma once

#include <filesystem>
#include "ultramodern/input.hpp"

namespace wdc::pak {
// Configure before starting guest threads. Only an absent file is formatted;
// unreadable or incorrectly sized images remain untouched and unavailable.
void configure(const std::filesystem::path& path, bool enabled = true);
ultramodern::input::connected_device_info_t device_info(int port);
}
