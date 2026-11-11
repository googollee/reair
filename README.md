# ReAir

ReAir 是一套面向公寓的分布式通风系统，主要目的是降低室内二氧化碳。集中式通风需要风管和有线控制器，后装困难；ReAir 改用多个可反转的风扇节点，节点之间无需有线连接，仅通过本地 Wi-Fi 协同，不依赖互联网。

- 每个节点都是独立的 Matter 设备，带外壳，可装在风道上，外壳上有配对二维码；可被 Home Assistant 等控制器单独控制。
- 节点有「主」「从」「独立」三种角色，可通过 Matter 切换，立即生效：
  - 主：设定风向模式、往复周期和风力，并向从节点同步风向和风力。
  - 从：跟随主节点的风向和风力，不能在从节点上直接修改；主节点离线时保持最后状态继续运行，重启后保持静止，直到收到主节点同步。
  - 独立：不同步也不跟随，单独设定风向与风力。
- 风向模式有固定正向、固定反向和往复切换，往复按设定周期正反切换，以节约能源；风力至少有高、中、低三档，关闭视为风力的特殊取值。
- 每个节点可选择「正向」对应进气还是排气（默认排气），并可只读查看当前实际风向（进气、排气或停止）。
- 系统只保证一个主节点：局域网内出现多个主节点时，节点记录警告状态，用户可通过 Matter 查看并清除。

不含传感器和自动控制策略，用户在 Matter 控制器中根据二氧化碳读数等自行编写规则；也不考虑局域网内的安全防护。详见 [docs/prd.md](docs/prd.md)。

## 目录

- `firmware/`：节点固件（ESP-IDF + esp-matter）
- `hardware/`：PCB 与外壳
- `config/`：配对信息、证书等配置
- `scripts/`：构建辅助脚本
- `docs/`：文档（PRD、设计、ADR），规则见 [docs/README.md](docs/README.md)
- `out/`：构建产物（由 make 生成）

## 构建

固件依赖 ESP-IDF 和 esp-matter，默认在 `~/esp` 下，可用 `IDF_DIR`、`ESP_MATTER` 覆盖。运行 `make help` 查看所有命令。

| 命令 | 说明 |
| --- | --- |
| `make env` | 准备固件编译环境（浅克隆 ESP-IDF 和 esp-matter，安装工具链，已装的跳过） |
| `make attestation` | 从 SDK 提取测试证书到 `config/attestation/test/` |
| `make config` | 根据 `config/` 生成 factory 分区镜像和 sdkconfig 片段 |
| `make build` | 编译固件（默认目标） |
| `make flash PORT=...` | 烧录固件和 factory 分区 |
| `make flash-factory PORT=...` | 只烧录 factory 分区 |
| `make pcb` | 导出 PCB 生产文件（Gerber、BOM、贴片坐标）到 `out/pcb/` |
| `make enclosure` | 渲染外壳各零件（3MF）到 `out/enclosure/`，需要 OpenSCAD |
| `make test` | 主机上的单元测试 |
| `make test-ota-rollback PORT=...` | OTA 回退的板上测试 |
| `make test-e2e` | 两块板的端到端同步测试 |
| `make clean` | 清除 `out/` |

常用变量：`PORT=/dev/cu.usbmodemXXXX`（烧录和板上测试）、`TARGET=esp32c3|esp32s3`（默认 `esp32c3`）。

典型流程：

```sh
make env
make build
make flash PORT=/dev/cu.usbmodemXXXX
```
