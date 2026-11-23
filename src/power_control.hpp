/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021-2022 YADRO.
 */

#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/container/flat_map.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <filesystem>
#include <list>
#include <string_view>

namespace power_control
{

/**
 * @brief Persistent State Manager
 *
 * This manager supposed to store runtime parameters that supposed to be
 * persistent over BMC reboot. It provides simple Get/Set interface and handle
 * default values, hardcoded in getDefault() method.
 * @note: currently only string parameters supported
 */
/* @brief 持久化状态管理器
 * 该管理器用于存储在 BMC 重启后仍需保留的运行时参数。
 * 它提供简单的 Get/Set 接口，并处理在 getDefault() 方法中
 * 硬编码的默认值。
 * @note: 目前仅支持字符串类型的参数 */
using dbusPropertiesList =
    boost::container::flat_map<std::string,
                               std::variant<std::string, uint64_t>>;
/**
 * @brief The class contains functions to invoke power restore policy.
 *
 * This class only exists to unite all PowerRestore-related code. It supposed
 * to run only once on application startup.
 */
/* @brief 该类包含用于调用 power restore 策略的函数。
 * 该类只是为了把所有与 PowerRestore 相关的代码集中在一起。
 * 它应当在应用启动时只运行一次。 */
class PowerRestoreController
{
  public:
    PowerRestoreController(boost::asio::io_context& io) :
        policyInvoked(false), powerRestoreDelay(-1), powerRestoreTimer(io),
        timerFired(false)
    {}
    /**
     * @brief Power Restore entry point.
     *
     * Call this to start Power Restore algorithm.
     */
    /* @brief Power Restore 入口点。
     * 调用此函数以启动 Power Restore 算法。 */
    void run();
    /**
     * @brief Initialize configuration parameters.
     *
     * Parse list of properties, received from dbus, to set Power Restore
     * algorithm configuration.
     * @param props - map of property names and values
     */
    /* @brief 初始化配置参数。
     * 解析从 dbus 收到的属性列表，以设置 Power Restore
     * 算法的配置。
     * @param props - 属性名称到属性值的映射 */
    void setProperties(const dbusPropertiesList& props);

  private:
    bool policyInvoked;
    std::string powerRestorePolicy;
    int powerRestoreDelay;
    std::list<sdbusplus::match> matches;
    boost::asio::steady_timer powerRestoreTimer;
    bool timerFired;
#ifdef USE_ACBOOT
    std::string acBoot;
#endif // USE_ACBOOT

    /**
     * @brief Check if all required algorithms parameters are set
     *
     * Call this after set any of Power Restore algorithm parameters. Once all
     * parameters are set this will run invoke() function.
     */
    /* @brief 检查是否所有必需的算法参数都已设置
     * 在设置任一 Power Restore 算法参数后调用此函数。当所有
     * 参数都设置好后，它会运行 invoke() 函数。 */
    void invokeIfReady();
    /**
     * @brief Actually perform power restore actions.
     *
     * Take Power Restore actions according to Policy and other parameters.
     */
    /* @brief 实际执行 power restore 动作。
     * 根据 Policy 及其他参数执行 Power Restore 动作。 */
    void invoke();
    /**
     * @brief Check if power was dropped.
     *
     * Read last saved power state to determine if host power was enabled before
     * last BMC reboot.
     */
    /* @brief 检查是否发生过掉电。
     * 读取上次保存的电源状态，判断在上次 BMC 重启之前
     * 主机电源是否处于开启状态。 */
    bool wasPowerDropped();
};

class PersistentState
{
  public:
    /**
     * List of all supported parameters
     */
    /* 所有支持的参数的列表 */
    enum class Params
    {
        PowerState,
    };

    /**
     * @brief Persistent storage initialization
     *
     * Class constructor automatically load last state from JSON file
     */
    /* @brief 持久化存储初始化
     * 类构造函数会自动从 JSON 文件加载上次保存的状态 */
    PersistentState();
    /**
     * @brief Persistent storage cleanup
     *
     * Class destructor automatically save state to JSON file
     */
    /* @brief 持久化存储清理
     * 类析构函数会自动将状态保存到 JSON 文件 */
    ~PersistentState();
    /**
     * @brief Get parameter value from the storage
     *
     * Get the parameter from cached storage. Default value returned, if
     * parameter was not set before.
     * @param parameter - parameter to get
     * @return parameter value
     */
    /* @brief 从存储中获取参数值
     * 从缓存存储中获取参数。如果该参数此前从未设置过，
     * 则返回默认值。
     * @param parameter - 要获取的参数
     * @return 参数值 */
    const std::string get(Params parameter);
    /**
     * @brief Store parameter value
     *
     * Set the parameter value in cached storage and dump it to disk.
     * @param parameter - parameter to set
     * @param value - parameter value to assign
     */
    /* @brief 存储参数值
     * 在缓存存储中设置参数值，并将其转储到磁盘。
     * @param parameter - 要设置的参数
     * @param value - 要赋给参数的值 */
    void set(Params parameter, const std::string& value);

  private:
    nlohmann::json stateData;
    const std::filesystem::path powerControlDir = "/var/lib/power-control";
    const std::string_view stateFile = "state.json";
    const int indentationSize = 2;

    /**
     * @brief Convert parameter ID to name
     *
     * Get the name corresponding to the given parameter.
     * String name only used by the manager internal to generate human-readable
     * JSON.
     * @param parameter - parameter to convert
     * @return parameter name
     */
    /* @brief 将参数 ID 转换为名称
     * 获取与给定参数对应的名称。
     * 字符串名称仅供管理器内部使用，用于生成可读性更好的
     * JSON。
     * @param parameter - 要转换的参数
     * @return 参数名称 */
    const std::string getName(const Params parameter);
    /**
     * @brief Get default parameter value
     *
     * Get the default value, associated with given parameter.
     * @param parameter - parameter to get
     * @return parameter default value
     */
    /* @brief 获取参数的默认值
     * 获取与给定参数关联的默认值。
     * @param parameter - 要获取的参数
     * @return 参数的默认值 */
    const std::string getDefault(const Params parameter);
    /**
     * @brief Save cache to file on disk
     */
    /* @brief 将缓存保存到磁盘上的文件 */
    void saveState();
};

} // namespace power_control
