// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright 2026 Intel Corporation

#pragma once

#include <boost/container/flat_map.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <regex>
#include <string>

namespace power_control
{

enum class ConfigType
{
    GPIO = 1,
    DBUS
};

struct ConfigData
{
    std::string name;
    std::string lineName;
    std::string dbusName;
    std::string path;
    std::string interface;
    std::optional<std::regex> matchRegex;
    bool polarity;
    ConfigType type;
};

/**
 * @brief Load and parse the power-control JSON configuration file.
 *
 * Opens @p configFile, parses its JSON content and populates the signal, timer
 * and event settings. The caller provides the path, so the file-location policy
 * stays with the caller and tests can point at their own file.
 *
 * @param configFile - path to the JSON configuration file
 * @param powerSignalMap - map of signal names to the ConfigData to populate
 * @param timerMap - timer values populated from the "timing_configs" section
 * @param eventConfigMap - values populated from the "event_configs" section
 * @return 0 on success, -1 on failure
 */
/* @brief 加载并解析 power-control 的 JSON 配置文件。
 * 打开 @p configFile，解析其 JSON 内容，并填充信号、定时器
 * 以及事件设置。路径由调用方提供，因此文件定位策略
 * 由调用方决定，测试可以指向自己的文件。
 * @param configFile - JSON 配置文件的路径
 * @param powerSignalMap - 信号名称到待填充 ConfigData 的映射
 * @param timerMap - 从 "timing_configs" 段填充的定时器值
 * @param eventConfigMap - 从 "event_configs" 段填充的值
 * @return 成功返回 0，失败返回 -1 */
int loadConfigValues(
    const std::filesystem::path& configFile,
    const boost::container::flat_map<std::string, ConfigData*>& powerSignalMap,
    boost::container::flat_map<std::string, int>& timerMap,
    boost::container::flat_map<std::string, bool>& eventConfigMap);

} // namespace power_control
