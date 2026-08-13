#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

// CommonLibSSE-NG generates __TradeTogetherPlugin.cpp with "..."sv.
// Keep the literal visible to that generated translation unit on MSVC.
using namespace std::string_view_literals;
