# CelebiHunter

<p align="center">
  <a href="https://github.com/Kou1236/CelebiHunter/releases/latest"><img src="https://img.shields.io/github/v/release/Kou1236/CelebiHunter?style=flat-square" alt="最新版本"></a>
  <a href="https://github.com/Kou1236/CelebiHunter/actions/workflows/tests.yml"><img src="https://github.com/Kou1236/CelebiHunter/actions/workflows/tests.yml/badge.svg?branch=main" alt="自动测试"></a>
  <a href="https://github.com/Kou1236/CelebiHunter/blob/main/LICENSE"><img src="https://img.shields.io/github/license/Kou1236/CelebiHunter?style=flat-square" alt="许可证"></a>
  <a href="https://github.com/Kou1236/CelebiHunter/releases"><img src="https://img.shields.io/github/downloads/Kou1236/CelebiHunter/total?style=flat-square" alt="下载量"></a>
</p>

面向在 Luma3DS 实机上游玩英文版 3DS Virtual Console《Pokémon Crystal Version》、希望获得闪光时拉比的玩家。CelebiHunter 提供全自动循环刷闪和玩家手动操作的 RNG 两种版本。

[English](README.md)

<p align="center">
  <img src="docs/images/celebi-hardware.jpg" alt="New 3DS 实机遇到闪光时拉比" width="720">
</p>

> [!IMPORTANT]
> Pokémon Bank 将于 [**2027 年 2 月 26 日**](https://en-americas-support.nintendo.com/app/answers/detail/a_id/61543/~/pok%C3%A9mon-bank-service-update)结束服务，届时从 Bank 传送至 Pokémon HOME 的功能也会停止。
>
> 在目前仍能把宝可梦传入 HOME 的系列正作中，[3DS VC《Pokémon Crystal Version》的 GS Ball 遭遇](https://www.pokemon.com/us/strategy/wrangle-rare-pokemon-in-pokemon-crystal)是唯一能让玩家自行捕捉闪光时拉比，并保留自己 OT、第二世代 Trainer ID 和 Game Boy 来源标志的途径。CelebiHunter 用于在这条传送路线关闭前完成这次捕捉。

## 版本

**Reset**

想按原版的 `1/8192` 概率刷闪，只是不想一直重复按键，就选 Reset。插件会自动跑完整套流程，但不保证多少轮能出闪。

**RNG**

想按预测的时机遇到闪光时拉比，就选 RNG。它在 [PokeReader](https://github.com/zaksabeast/PokeReader) 的时拉比 RNG 方法上做了改进，直接显示当前 Advance 和下一个闪光目标。你自己暂停、逐帧推进，再按 A 继续事件。

只想尽快拿到闪光、不在意 RNG 流程的话，也可以选 [旧版 RNG v1.0.0](https://github.com/Kou1236/CelebiHunter/releases)。旧版会修改游戏读取到的 DIV 值，固定生成 `FAAA` 闪光组合。

**一次只安装一个版本。**

## 兼容范围

- 英文版 3DS VC《Pokémon Crystal Version》
- Title ID：`0004000000172800`
- Ilex Forest Shrine（桐树林神龛）GS Ball 时拉比事件
- 已启用 Plugin Loader 的 Luma3DS
- Reset v1.0.0：Old 3DS、Old 2DS、New 3DS、New 2DS，268 MHz、804 MHz
- RNG v1.2.0：已在 New 3DS、268 MHz、Luma3DS 13.4 上实机测试

主要测试环境：New 3DS、268 MHz、Luma3DS 13.4。RNG 版的其他机型、CPU 模式和 Luma3DS 版本尚未实测。欢迎通过 GitHub Issues 反馈实机测试结果。

日版 Crystal 和其他宝可梦遭遇**暂未支持**，后续会考虑扩展。

## 安装

1. 在 Ilex Forest Shrine（桐树林神龛）正前方，面向神龛并在放入 GS Ball 前保存游戏。
2. 退出游戏，用 Checkpoint 备份存档，并在电脑上保留一份原始副本。
3. 从最新 GitHub Release 下载 `Reset.3gx` 或 `RNG.3gx`。
4. 把选中的文件放入：

   ```text
   sd:/luma/plugins/0004000000172800/
   ```

5. 确认该目录内只有一个 `.3gx` 文件。
6. 按 `L + 十字键下 + Select` 打开 Rosalina，启用 **Plugin Loader**。
7. 启动 Pokémon Crystal。

## Reset

启动游戏后不要操作。Reset 会自动进入 Continue（继续游戏）、触发神龛并检查时拉比。

<p align="center">
  <img src="docs/images/reset-result.png" alt="Reset 找到闪光时的结果画面" width="400">
</p>

- **非闪：**自动开始下一轮。
- **闪光：**停止并保留当前遭遇。
- `B`：停止自动流程。
- `R`：遇到闪光或按 `B` 停止后，恢复游戏并隐藏结果卡片。

## RNG

到达 GS Ball 最终文本后，插件会自动寻找下一个闪光时机。透明悬浮窗会显示当前 Advance、目标 Target、预测 DV 和实际 DV。

<p align="center">
  <img src="docs/images/rng-trigger.png" alt="GS Ball 事件文本" width="400">
</p>

1. 手动来到神龛并推进对话。
2. 当 `[PLAYER] put in the GS BALL.` 完整显示时，松开 A。
3. 按 `Start + ↑` 显示或隐藏悬浮窗。
4. 接近 Target 时，按 `L + R` 暂停，再按 `L` 逐帧推进。想继续正常运行时，按 `R`。
5. 当 Advance 等于 Target 时，按 A 继续事件，松开 A，等时拉比出现。悬浮窗会显示这次遭遇的实际 DV。

错过目标时，插件会寻找下一个闪光时机。等待过程中游戏状态改变，目标也可能更新，以悬浮窗当前显示的 Target 为准。按 A 后不要操作其他按键，等时拉比出现。

工作原理详见 [RNG 的工作原理](#rng-的工作原理)。

### 传送到后续作品后

- 这些 DV 不会直接带到后续作品。
- Poké Transporter 会给时拉比**五项 31**，剩下一项随机。
- 第二世代没有性格。刚捕获且没有获得过经验的等级 30 时拉比会是 **Timid（胆小）**；传送前练过级则可能不同。
- 自己的 **OT 和第二世代 Trainer ID** 会保留，Secret ID 变为 `00000`。
- 球种是普通 Poké Ball（精灵球），特性是 Natural Cure（自然回复），并带有 **Game Boy 来源标志**。
- 从 Bank 传到 HOME 时不会改变这些内容。

完整转换规则：[Poké Transporter](https://bulbapedia.bulbagarden.net/wiki/Pok%C3%A9_Transporter#From_Generation_I_and_II)

## 文档

- [编译](docs/BUILDING.md)
- [备份与恢复](docs/SAFETY.md)
- [开发计划](docs/ROADMAP.md)
- [参与开发](CONTRIBUTING.md)

## 许可

CelebiHunter 采用 [GNU GPL v3.0 or later](LICENSE)。

## RNG 的工作原理

《Crystal》的 [`Random` 例程](https://github.com/pret/pokecrystal/blob/master/home/random.asm)会读取 Game Boy 的分频寄存器 `DIV`，用来更新 RNG 状态。RNG 版读取当前 RNG、DIV 和模拟器的计时状态，计算继续等待、按 A 后会生成什么 DV，再寻找其中的闪光时机。

为了让过场的计时可以预测，插件会设置 RTC、GameTime、TIMA，并调整模拟器的执行计时，包括时钟预算和相位。这些设置会改变游戏的计时环境，但时拉比的生成仍沿用原游戏的遭遇流程。

这一版以 PokeReader 的方法为基础，补充了计时状态的读取，并简化了搜索计算。参数读取和目标搜索都在插件内完成，不需要再等待索引或去网站查 Advance。时拉比出现后，实际 DV 也会显示在悬浮窗里。

## 致谢

感谢以下项目及其作者：

- [PokeReader](https://github.com/zaksabeast/PokeReader) 和 [Pokémon RNG Guides](https://github.com/zaksabeast/PokemonRNGGuides)（zaksabeast）：游戏读取和时拉比 RNG 预测方法。
- [pret/pokecrystal](https://github.com/pret/pokecrystal)：Crystal 反汇编源码和符号。
- [Pan Docs](https://gbdev.io/pandocs/)（gbdev）：Game Boy 计时器和中断资料。
- [Luma3DS](https://github.com/LumaTeam/Luma3DS)（LumaTeam）：3GX 插件加载器和 Rosalina 调试器。
- [CTRPluginFramework](https://gitlab.com/thepixellizeross/ctrpluginframework)（The Pixellizer Group）与 [Blank Template](https://github.com/PabloMK7/CTRPluginFramework-BlankTemplate)（PabloMK7）：插件接口和 3GX 构建参考。
