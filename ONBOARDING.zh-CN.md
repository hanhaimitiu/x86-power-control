# x86-power-control 代码导读（OpenBMC 新人向）

> 面向刚转入 OpenBMC / openubmc 开发的同学。建议先了解：x86 服务器电源时序
> （PWRBTN/SIO_ONCTL/SIO_S5/POWER_GOOD 等信号）、libgpiod v2、
> phosphor-state-manager（本文常与其对照）。

## 1. 这个仓库是干什么的

x86 平台的**电源控制守护进程**：BMC 通过操作/监听一组电源控制 GPIO，
实现硬开机/关机/复位/上下电循环，并维护内部电源状态机、
把状态同步到 D-Bus（`State.Chassis`、`State.Host` 的 RestartCause、按钮接口等）。

与 phosphor-state-manager 的关系：state-manager 是"状态权威 + systemd 编排"，
x86-power-control 是**具体平台怎么拉电源脚**的实现层——它自己发
`State.Chassis.Transition` 的 D-Bus 请求或直接操作 GPIO，再把结果写回。

三个设计目标（README）：BMC 内部维护 Host 状态机；操作要么成功要么记录错误日志；
支持硬/软的 on/off/cycle 全套操作。

一句话：**所有"按电源键/强制断电/复位"的 x86 落地逻辑都在这一个文件里。**

## 2. 目录结构

```
├── src/power_control.cpp    # 几乎全部逻辑（约 3600 行，单文件状态机）
├── src/power_control.hpp
├── config/
│   └── power-config-host0.json  # 平台信号定义：每个信号是 GPIO 还是 D-Bus、
│                                # 用哪根线/哪个对象、极性、时延参数
└── service_files/
    ├── xyz.openbmc_project.Chassis.Control.Power@.service
    └── chassis-system-reset.{service,target}   # 系统级复位按钮链路
```

平台适配 = bbappend 一份自己的 `power-config-host0.json`，不改代码。

## 3. 核心机制

### 3.1 信号抽象：GPIO 或 D-Bus

JSON 配置把每路信号声明为两种类型之一（README 有示例）：
- `GPIO`：直接 libgpiod 线名（如 `POWER_BUTTON`、`RESET_BUTTON`、`NMI_BUTTON`、
  `POWER_OK`、`SIO_POWER_GOOD`、`SIO_ONCTL`、`SIO_S5`、`POST_COMPLETE`、`NMI_OUT`）；
- `DBUS`：代理到别的 D-Bus 对象（如虚拟按钮服务）。

代码里统一用 `gpiod::line` + `boost::asio::posix::stream_descriptor` 持有每路信号
（src/power_control.cpp:188 起），监听用 `requestGPIOEvents()`（1129 行），
输出用 `setGPIOOutput()` / `assertGPIOForMs()`（1266 行，脉冲拉高/拉低 N 毫秒）。

### 3.2 电源状态机

- 全局 `powerState`，每个状态有 handler（`getPowerStateHandler`）；
- 所有操作收敛到 `sendPowerControlEvent(Event)`（src/power_control.cpp:467）：
  当前状态 + 事件 → 对应 handler 决定下一步（拉哪根线、起哪个定时器、迁到哪个状态）；
- 主要动作函数：`powerOn()`（1271）、`slotPowerCycle()`（1323）、
  `gracefulPowerOff()`（1347，模拟短按电源键）、`forcePowerOff()`（1352，长按 4 秒）、
  `reset()`（1377）。

### 3.3 定时器与看门狗（出问题的检查重点）

| 函数 | 用途 |
|---|---|
| `gracefulPowerOffTimerStart`（1382） | 软关机超时 → 升级为硬断电 |
| `powerCycleTimerStart`（1405） | power cycle 的断电间隔 |
| `powerOKWatchdogTimerStart`（1428） | POWER_OK/SIO_POWER_GOOD 没起来 → 报 VR 失败 |
| `warmResetCheckTimerStart`（1451） | 复位后判断是否热复位 |
| `pohCounterTimerStart`（1474） | PowerOnHours 计数 |

失败路径统一用 `sd_journal_send` 打 Redfish 消息
（如 `OpenBMC.0.1.SystemPowerGoodFailed`，src/power_control.cpp:709）——
这是 OpenBMC 里"日志触发 Redfish 告警"的标准模式，值得记住。

### 3.4 对外 D-Bus 接口（add_interface 清单，src/power_control.cpp:3040 起）

| 服务/对象 | 接口 | 说明 |
|---|---|---|
| `/xyz/openbmc_project/state/chassis<N>` | `State.Chassis` | 上报 `CurrentPowerState`、转发 transition |
| `/xyz/openbmc_project/state/host<N>` | `State.Host` | `RestartCause` 等属性 |
| `/xyz/openbmc_project/chassis<N>` | Chassis 系统/槽位接口 | 平台相关 |
| `/xyz/openbmc_project/chassis<N>/buttons/power_button` 等 | `Chassis.Buttons` | 按钮状态（Pressed） |
| NMI Out / OS Status | `Chassis.Control.NMISource`、`OperatingSystem.Status` | NMI 注入、OS 状态 |

同时它也**消费**别家的状态：读 `State.Host.HostState`（判断主机是否在运行）、
`Settings`（电源恢复策略等）、监听按钮服务事件。

### 3.5 状态机全景（改状态机前先看这）

10 个 `PowerState` 与 handler 的对应表（`getPowerStateHandler`，src/power_control.cpp:438）：

| 状态 | 含义 |
|---|---|
| `on` | 已上电稳定运行 |
| `waitForPowerOK` / `waitForSIOPowerGood` | 发出上电动作后等电源好信号 |
| `off` | 断电稳定 |
| `transitionToOff` / `gracefulTransitionToOff` | 硬关机中 / 软关机中（软超时会升级为硬） |
| `cycleOff` / `transitionToCycleOff` / `gracefulTransitionToCycleOff` | power cycle 的断电驻留及进入过程 |
| `checkForWarmReset` | 复位后判断是热复位还是冷启动（`warmResetCheckTimerStart`） |

状态 → 对外属性的映射（两个 constexpr，读代码时容易反直觉）：
- `getHostState`（487 行）：`on` 和 `gracefulTransitionToOff/…CycleOff` 都报
  **Host Running**——软关机期间 OS 还活着；`waitForPowerOK` 起报 Off；
- `getChassisState`（507 行）：`on` 和**所有 transitionTo\*/checkForWarmReset** 报
  **Chassis On**（正在下电也认为机箱通电），`off/cycleOff/waitFor*` 报 Off。

其余值得认识的全局机制（都在 power_control.cpp）：
- `setPowerState`（574）/ `savePowerState`（554）：状态迁移 + 持久化（掉电恢复判断用）；
- `addRestartCause`（623）/ `setRestartCause`（665）/ `resetACBootProperty`（644）：
  维护 `State.Host.RestartCause`（AC 上电、命令、电源键、watchdog...），
  同时读 Settings 的 PowerRestorePolicy；
- `currentHostStateMonitor`（1537）：监听 phosphor-state-manager 的 Host 状态变化，
  反向对账自家状态机；
- `beep`（214）：按优先级响蜂鸣器；
- `setOperatingSystemState`（252）：更新 `OperatingSystem.Status`；
- `parseEventConfig`（2393）：解析 power-config-host0.json（GPIO/DBUS 两种信号源在此统一为 `ConfigData`）；
- `CHASSIS_SYSTEM_RESET` 编译块（527 起）：多槽位机器的 `SlotPowerState`
  子状态机（slotPowerOn/Off/Cycle，1276-1345），配合 service_files 里的
  chassis-system-reset.target。

## 4. 关键流程举例：短按电源键软开机

```
用户请求 On（D-Bus transition / Web / IPMI）
 → sendPowerControlEvent(PowerOn)
 → 当前状态 handler：powerOn()
 → 按 JSON 配置拉起 SIO_ONCTL / PWRBTN 脉冲（assertGPIOForMs）
 → 等 SIO_POWER_GOOD / POWER_OK 上升沿（GPIO 事件回调）
 → 起看门狗：超时没 good → systemPowerGoodFailedLog()
 → good 到了 → powerState 迁移 → 更新 State.Chassis.CurrentPowerState
```

## 5. 构建与测试

meson；单测极少（单文件状态机主要靠整机联调），配置生成见 `meson.options` 与
`config/`。无 gtest 时，调试手段：
- journal 搜 `PowerControl:` 前缀日志；
- `gpioinfo | grep -E "POWER|SIO|RESET"` 看线占用；
- `busctl get-property ... State.Chassis CurrentPowerState` 对账。

## 6. 新手学习路径

1. `README.md` —— 信号定义与设计目标；
2. `config/power-config-host0.json` —— 你的平台有哪些信号、在哪；
3. `src/power_control.cpp` 的 `main()`（3000 行以后，接口注册一览）；
4. `sendPowerControlEvent` + `getPowerStateHandler` —— 状态机入口；
5. `powerOn()` / `forcePowerOff()` —— 两条最常用的动作路径。

读单文件大仓库的技巧：先按上面行号当"目录"跳读，再顺着 GPIO 名字全局搜。

## 7. 常见改动场景

- **换平台**：只改 JSON 配置（bbappend），核对线名/极性/时延；
- **上电失败**：看门狗日志 → 查 POWER_OK 监听是否与 JSON 线名一致；
- **加新信号**：JSON 加定义 + 代码里按 GPIO/DBUS 类型补 requestGPIOEvents/setGPIOOutput。
