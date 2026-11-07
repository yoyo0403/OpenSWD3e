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
- 结果（本机 2026-11-07，畫面濾鏡改四档 nearest／pixel_art／linear／edge：pixel_art 用 SDL 的 SDL_SCALEMODE_PIXELART；edge 时地图两帧之间回 CPU 交错画法并禁用菜单背景读回；修复合并 smooth-gpu 的两处遗漏——Present 的 behind 分支与 EndLayers 滤镜取样；繪製倍率放宽到与 nearest／pixel_art 同开）：重建成功（仅重编受影响目标），0 个编译警告，clang-format 无差异，风格检查六项规则无违反（本机无 ripgrep，以等价命令对改动行执行）。单元测试 179 项：与改动相关的 display／host settings 用例全部通过；失败 21 个断言集中在 3 个用例，均与本改动无关——glyph library 9 项（本机 font.bin，10-31 已记录）、field 的 HUGE.lmf 1 例（测试用 create_symlink 建临时链接，本会话非管理员无符号链接权限，链接未建成所致）、ghost 的 swdghost.dat 1 例（游戏目录的 swdghost.dat 当日 10:03 被游玩改写，与黄金值不符）。后两者在管理员会话／未游玩的游戏目录下应恢复。待用户试玩四种滤镜观感。
- 结果（本机 2026-11-07，edge 判角加 1px 细节保护：两侧区域延伸判定（远侧半延伸时混色减半）与对角相触规则，见 deviations.md hd-scaler 条；src/platform/edge_scaler.cpp 与 tests/edge_scaler_test.cpp，新增 5 个用例）：重建成功（仅重编 edge_scaler、display 与测试目标），0 个编译警告，clang-format 无差异，风格检查对改动文件无违反。单元测试 184 项：edge scaler 12 个用例全部通过；失败 9 个断言仍为 glyph library（本机 font.bin，10-31 已记录）。待用户试玩 edge 滤镜下行走小人与文字观感。
- 结果（本机 2026-11-07，新增第五档 grid 像素网格：src/platform/grid_scaler.cpp（原色不混合，仅块东、南缝子像素压暗 20/256，缝角两次，分块多线程），FrameFilter 加 Grid、FrameFilterScalesOnCpu 谓词泛化 edge 专属分支（display.cpp Present/SetFilter/SetBackdrop、game.cpp CaptureMapBehind/LayBetween），设置页加「網格」；测试 tests/grid_scaler_test.cpp 4 例）：重建成功（重编受影响目标），0 个编译警告，clang-format 无差异，风格检查对改动文件无违反（长行按 UTF-8 字符数计；Git Bash awk 的 C locale 按字节会把 CJK 行误报，WSL 无此问题）。单元测试 188 项：grid scaler 4 例与 display／host settings 滤镜用例全部通过；失败 9 个断言仍为 glyph library（本机 font.bin，10-31 已记录）。基准（经测试目标计时，640×480，grid 的开销与内容无关）：factor 2 2.45ms、factor 3 3.41ms、factor 4 4.53ms 一帧，均快于 edge。预览图 tmp/grid_font_3.png、tmp/grid_sprite_2.png、tmp/grid_sprite_3.png（左 nearest、右 grid）。待用户试玩网格档观感与缝强度（kSeamDim）。
- 结果（本机 2026-11-07，方案 B「edge／grid 下保留深度缩放角色的交织细节」：两帧之间与菜单背后在 edge／grid 下改按滤镜整数倍（FilterFactor 2..4）交织重画，EdgeScaler／GridScaler 新增 ScaleWoven（判角取每块西北角样本＝整像素画法那帧，输出子像素取交织画布各自的样本，均匀块输出与原先逐像素一致），Present／SetBackdrop／MakeFilteredFrame／MakeFilteredBackdrop 加 parts 参数、帧缓存键含 parts，Game::wovenBackdrop 为菜单背景的交织快照；战斗两帧之间仍按 1）：重建成功，0 个编译警告，clang-format 无差异，风格检查对改动文件无违反（check_style.sh 的六项规则以等价命令执行，本机无 ripgrep）。单元测试 193 项：新增交织用例 5 个（edge 3：均匀块与逐像素输出全等、孤块保各自样本、拐角从自身样本混色；grid 2：均匀块全等、各样本自带缝色）全部通过；失败 9 个断言仍为 glyph library（本机 font.bin，10-31 已记录）。调试中修掉 grid ScaleWoven 一处把样本索引隐式截断为 u16 的笔误（Expand 收到索引而非像素）。待用户试玩：edge／grid 下大地图精灵的清晰度应与 nearest 相当（静止时同样清楚），走路与镜头卷动照旧，注意 3～4 倍窗口下的 CPU 占用。
- 结果（本机 2026-11-07，畫面濾鏡按用户要求缩减为 nearest／edge 两档：pixel_art／linear／grid 移除（grid_scaler 源码与测试删除），FrameFilter 谓词（TakesScale／ScalesOnCpu）与 FrameSampling 删去改为直接比较，渲染器侧纹理恢复创建时的最近邻取样；设置页、host_settings 校验、swd3er.toml 与 docs/index.html 同步）：重建成功（重编受影响目标），0 个编译警告，clang-format 无差异，风格检查六项规则无违反（本机无 ripgrep，以 .NET 正则同语义执行；awk 字节计长的 CJK 误报与 PowerShell -match 默认不区分大小写的误报均已排除）。单元测试 187 项：edge scaler（含交织 3 例）、display 与 host settings 滤镜用例全部通过；失败 21 个断言仍集中在 3 个已知环境用例（glyph library 9、field 的 HUGE.lmf、ghost 的 swdghost.dat，原因见当日早前记录）。待用户试玩两种滤镜观感。
- 高清滤镜基准（tmp/edge_scaler_bench.cpp，-O2，640×480，最坏情况全画面 4×4 棋盘格）：factor 2 4.56ms、factor 3 4.79ms、factor 4 6.41ms 一帧；实际画面以平坦区为主（快路径），显著更快。（2026-11-07 加延伸判定后复测，同场景经测试目标计时：factor 2 6.50ms、factor 3 7.33ms、factor 4 8.06ms 一帧；最坏情况变慢约 30～50%，平坦区与快路径不受影响。）
