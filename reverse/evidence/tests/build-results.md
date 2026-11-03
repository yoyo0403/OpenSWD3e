# 构建与测试记录

构建结果、测试结果与逐函数汇编验证结论分开记录；测试结果只作辅助，不代替逐函数对照（证据见 reverse/evidence/pass1/）。

## Linux（x86_64 静态，WSL）

- 入口：`cmake --preset linux`、`cmake --build --preset linux`（构建目录 build/linux/，工具链 cmake/toolchain-linux-x64-clang.cmake）；单元测试 `ctest --preset linux` 或直接运行 `build/linux/swd3e_tests`。
- 结果（第二遍 1710／1710 完成时的工作树，2026-10-29）：构建成功；单元测试 172 项全部通过（`172 test cases, 0 failures, 0 pending swd3e`）。正传黄金结果的处理见 legacy-goldens.md。

## Windows（x64，WSL 中以 clang-cl／lld-link 交叉构建）

- 入口：`cmake --preset windows`、`cmake --build --preset windows`（构建目录 build/windows/，工具链 cmake/toolchain-windows-x64-clang-cl.cmake，MSVC 14.44 与 Windows SDK 10.0.26100 的头文件和库）。
- 产物：build/windows/swd3er.exe（游戏），build/windows/swd3e_tests.exe（单元测试），build/windows/swd3er.toml（开发用配置，不入库：data_dir = E:\Game\swd3e，save_dir = E:\Game\swd3e\save，日志 logs/swd3er-<日期_时间>-<PID>.log），build/windows/asset/。
- 结果（同上）：构建成功，0 个编译警告（`grep -c 'warning:' tmp/build_windows.log` 为 0）。
- Windows 单元测试与试玩由用户运行，结果回传后记入本节。

| 项目 | 状态 |
| --- | --- |
| Windows 单元测试（swd3e_tests.exe） | 待用户运行 |
| Windows 试玩（swd3er.exe） | 待用户运行 |

## 原生 Windows 构建（无 WSL；scripts/native-windows/，2026-10-31 高清滤镜改动）

- 入口：`scripts/native-windows/build_main.sh`（MSYS2/Git Bash；工具链同上，LLVM 23.1.4 于 C:\Program Files\LLVM，MSYS2 bash 与 junction 见 windows编译.md）。
- 结果（本机 2026-10-31，工作树含 [display] scaler 高清滤镜改动）：构建成功，0 个编译警告；单元测试 179 项中 178 项通过（`179 test cases, 9 failures`——失败全部为 "glyph library: MingLiU cells of the sizes the game draws" 的 9 个断言）。该失败与本改动无关：stash 掉改动后重建同样失败（`177 test cases, 9 failures`），属本机 font.bin 资源与测试期望不符，待单独核实。
- 结果（本机 2026-11-02，叠加状态栏好感显示改动 src/menu/menu.cpp）：构建成功（仅重编 menu.cpp），0 个编译警告；风格检查（check_style.sh 的六项规则以等价命令逐项执行，本机无 ripgrep）与 clang-format --dry-run 无差异。单元测试与试玩待用户运行。
- 结果（本机 2026-11-02，好感数值右对齐修正——剧本 talk2 事件 4／talk3 事件 11 各强制 SET 100，后半段数值为三位数会超出面板右缘）：重建成功，0 个编译警告，clang-format 无差异。待用户试玩确认。
- 高清滤镜基准（tmp/edge_scaler_bench.cpp，-O2，640×480，最坏情况全画面 4×4 棋盘格）：factor 2 4.56ms、factor 3 4.79ms、factor 4 6.41ms 一帧；实际画面以平坦区为主（快路径），显著更快。
