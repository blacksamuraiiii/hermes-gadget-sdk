# 验收：pytest 全量基线与逐项分类（moji-0.3）

- 日期：2026-10-30
- 命令：`HGSIM_LIBRARY=$PWD/build/libhgsim.so ~/.venv/bin/python3 -m pytest tests -q`
- 前置：`cmake -S firmware -B build -DHG_BUILD_TESTS=ON && cmake --build build --target hgsim hg_core_tests`（补齐模拟器库）
- 结果：**7 failed / 380 passed / 56 skipped / 5 errors**，耗时 8 分 04 秒

## 逐项结论

| # | 用例 | 结论 | 依据 |
|---|---|---|---|
| 1 | `test_face.py::test_picker_command_runs_in_the_shell_and_preserves_the_picture[False]` | 环境项（非 fork 回归） | 用例要在 shell 里跑 `hermes-gadget` 命令，本机 venv 没装这个包；`环境-本机工具链与坑.md` 记了替身脚本放 PATH 即过 |
| 2 | 同上 `[True]` | 环境项 | 同上 |
| 3 | `test_linux.py::test_service_socket_is_private_exclusive_and_stops_on_sigterm` | 环境项 | 在改动前提交 `e3bb447` 的 worktree 上同样失败（子进程 `poll()` 返回 1），与本次改动无关 |
| 4 | `test_sim_hub.py::test_screens_render_deterministically` | flaky（已知） | 同一树同一库连跑两次均过；首轮全量跑偶尔失败，未定性为代码缺陷 |
| 5 | `test_versions.py::test_every_file_carries_the_same_version` | **fork 回归，已修** | 把 `PROJECT_VER` 改成 0.3.0 后其余三处仍是 0.2.0；提交 `a6c0a2b` 补齐，`tools/check_versions.py` 现报 `0.3.0 in all 4 files` |
| 6 | `test_versions.py::test_the_tag_must_match` | **fork 回归，已修** | 同上（版本不一致连带 tag 比对失败） |
| 7 | `test_versions.py::test_the_tag_needs_its_changelog_section` | **fork 回归，已修** | 打 tag 要求 CHANGELOG 有 `## 0.3.0`；提交 `a6c0a2b` 新开该段 |
| 8–12 | `test_sim_window.py` 5 条 ERROR | 环境项 | 夹具依赖 Tk 图形窗口，本机 WSL 无显示；属「跑不了」而非「跑挂了」 |

## 发版说明写法

「core 298 例全过；pytest 补齐模拟器库后 380 过 / 7 失败，其中 3 项是本次版本号同步的回归（已修，`a6c0a2b`）、4 项为环境项（缺 `hermes-gadget` CLI ×2、linux 服务用例、Tk 窗口 ×5 记 error）与 1 项已知 flaky。」——不写「测试全过」。
