# 运行 make help 查看所有命令。
# 依赖 ESP-IDF 和 esp-matter,默认在 ~/esp 下,可用 IDF_DIR、ESP_MATTER_PATH 覆盖。

SHELL := /bin/zsh

ROOT       := $(CURDIR)
IDF_DIR    ?= $(HOME)/esp/esp-idf
IDF_BRANCH ?= release/v6.0
ESP_MATTER_BRANCH ?= release/v1.6.1
ESP_MATTER ?= $(HOME)/esp/esp-matter
# IDF 的 Python 环境按「IDF 版本 + 当前 python3 的版本」命名,跟着 PATH 里的 python3 走,不写死版本;
# 用别的环境时可覆盖 IDF_PY_ENV
IDF_VER    := $(shell sed -nE 's/^set\(IDF_VERSION_(MAJOR|MINOR) ([0-9]+)\)/\2/p' $(IDF_DIR)/tools/cmake/version.cmake 2>/dev/null | head -2 | paste -sd. -)
PY_VER     := $(shell python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])' 2>/dev/null)
IDF_PY_ENV ?= $(HOME)/.espressif/python_env/idf$(IDF_VER)_py$(PY_VER)_env
FW_DIR     := $(ROOT)/firmware
OUT        := $(ROOT)/out
HW_DIR     := $(ROOT)/hardware
TARGET     ?= esp32c3
PY         := $(IDF_PY_ENV)/bin/python
# 外壳只需要 PyYAML,没有 ESP-IDF 的 Python 环境时用系统的 python3
ENC_PY     ?= $(if $(wildcard $(PY)),$(PY),python3)
ENC_SRC    := $(HW_DIR)/enclosure/enclosure.scad
ENC_OUT    := $(OUT)/enclosure
OPENSCAD   ?= openscad
OPENSCAD_FLAGS ?=
comma := ,
space := $(empty) $(empty)
ENC_PARTS  := back cap strip btn pipe mid_body mid_island

# 必须先 source ESP-IDF,再 source esp-matter
ENV := . $(IDF_DIR)/export.sh >/dev/null 2>&1; . $(ESP_MATTER)/export.sh >/dev/null 2>&1

# 这两个文件决定 sdkconfig 的内容;IDF 只在 sdkconfig 不存在时才应用 defaults,
# 所以它们变了就要删掉旧的 sdkconfig 重新生成
CONFIG_INPUTS := $(FW_DIR)/sdkconfig.defaults $(wildcard $(FW_DIR)/sdkconfig.defaults.*) $(OUT)/sdkconfig.device
STAMP         := $(OUT)/.sdkconfig.stamp
# 构建目录和 sdkconfig 都放在 out/ 下(managed_components/ 和 dependencies.lock 由 IDF 组件管理器固定写在 firmware/)
FW_BUILD      := $(OUT)/build
IDF_OUT       := -B $(FW_BUILD) -DSDKCONFIG=$(OUT)/sdkconfig

.DEFAULT_GOAL := build
.PHONY: help env config build flash flash-factory attestation pcb enclosure test test-e2e test-ota-rollback clean

help: ## 列出所有命令
	@awk 'BEGIN {FS = ":.*## "} /^[a-zA-Z_-]+:.*## / {printf "  make %-14s %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@echo "  变量: PORT=/dev/cu.usbmodemXXXX (flash 系列)  TARGET=esp32s3|esp32c3"

env: ## 准备固件编译环境(浅克隆 ESP-IDF 和 esp-matter,装 esp32c3 工具链,已装的跳过)
	@IDF_DIR=$(IDF_DIR) ESP_MATTER=$(ESP_MATTER) IDF_BRANCH=$(IDF_BRANCH) ESP_MATTER_BRANCH=$(ESP_MATTER_BRANCH) $(if $(filter file,$(origin IDF_PY_ENV)),,IDF_PY_ENV=$(IDF_PY_ENV)) bash scripts/setup_env.sh

attestation: ## 从 SDK 提取测试证书到 config/attestation/test/
	ESP_MATTER_PATH=$(ESP_MATTER) $(PY) -I scripts/extract_test_attestation.py

config: ## 根据 config/ 生成 factory 分区镜像和 sdkconfig 片段
	ESP_MATTER_PATH=$(ESP_MATTER) $(PY) -I scripts/gen_commissioning.py --target $(TARGET)

build: config ## 编译固件
	@new=$$(cat $(CONFIG_INPUTS) | shasum | cut -d' ' -f1); \
	if [ "$$new" != "$$(cat $(STAMP) 2>/dev/null)" ]; then \
	  echo "sdkconfig.defaults 或 config/ 有变化,重新生成 sdkconfig"; \
	  rm -f $(OUT)/sdkconfig; \
	fi; \
	$(ENV); cd $(FW_DIR) \
	  && { test -f $(OUT)/sdkconfig || idf.py $(IDF_OUT) set-target $(TARGET); } \
	  && idf.py $(IDF_OUT) -DPROJECT_VER_NUMBER=$$(date +%y%m%d%H) -DREAIR_OTA_TEST_MODE=0 -DREAIR_TEST_PROJECT_VER= build \
	  && echo "$$new" > $(STAMP)

flash-check:
	@test -n "$(PORT)" || { echo "请指定串口,例如 make flash PORT=/dev/cu.usbmodemXXXX"; exit 1; }

flash: build flash-check ## 烧录固件和 factory 分区(需要 PORT)
	$(ENV); cd $(FW_DIR) && idf.py $(IDF_OUT) -p $(PORT) flash
	$(MAKE) flash-factory PORT=$(PORT)

flash-factory: flash-check config ## 只烧录 factory 分区(需要 PORT)
	$(ENV); off=$$(sed -n 's/^CONFIG_PARTITION_TABLE_OFFSET=//p' $(OUT)/sdkconfig); \
	  $(PY) $(IDF_DIR)/components/partition_table/parttool.py -p $(PORT) --partition-table-offset $$off \
	  write_partition --partition-name fctry --input $(OUT)/factory_partition.bin

pcb: ## 导出 PCB 生产文件(Gerber、BOM、贴片坐标)到 out/pcb/,先跑 DRC
	$(PY) -I scripts/gen_pcb_production.py $(ARGS)

# 外壳:把 config/ 里的配对信息(含 device.local.yaml 的覆盖)用 -D 传给 OpenSCAD,
# 渲染各零件到 out/enclosure/(3MF,带颜色)。不需要 SDK,需要 OpenSCAD 和 PyYAML。
enclosure: ## 用 config/ 的配对码渲染外壳各零件(3MF,带颜色)到 out/enclosure/,并校验二维码
	@command -v $(OPENSCAD) >/dev/null || { echo "找不到 OpenSCAD(brew install openscad,或 make enclosure OPENSCAD=路径)"; exit 1; }
	@mkdir -p $(ENC_OUT)
	@$(ENC_PY) scripts/gen_enclosure_data.py --defines >/dev/null
	for p in $(ENC_PARTS); do \
	  $(OPENSCAD) $(OPENSCAD_FLAGS) -o $(ENC_OUT)/$$p.3mf $$($(ENC_PY) scripts/gen_enclosure_data.py --defines 2>/dev/null) -D "part=\"$$p\"" $(ENC_SRC) 2>$(ENC_OUT)/$$p.log \
	    || { cat $(ENC_OUT)/$$p.log; exit 1; }; \
	done
	$(ENC_PY) scripts/pack_enclosure_3mf.py $(ENC_OUT)/mid.3mf $(ENC_OUT)/mid_body.3mf $(ENC_OUT)/mid_island.3mf
	$(ENC_PY) scripts/gen_enclosure_data.py --verify $(ENC_OUT)/mid_body.log
	@echo "已生成 $(ENC_OUT)/{back,mid,cap,strip,btn,pipe}.3mf(mid.3mf 里二维码黑色岛是单独的黑色体,mid_body/mid_island 是它的中间文件)"

test: ## 主机上的单元测试(同步报文编解码、按键判定、风扇占空比映射、OTA 判定)
	@mkdir -p $(OUT) && g++ -std=c++17 -Wall -o $(OUT)/sync_packet_test $(FW_DIR)/test/sync_packet_test.cpp && $(OUT)/sync_packet_test
	@g++ -std=c++17 -Wall -Wextra -Werror -o $(OUT)/button_logic_test $(FW_DIR)/test/button_logic_test.cpp && $(OUT)/button_logic_test
	@g++ -std=c++17 -Wall -Wextra -Werror -o $(OUT)/fan_duty_test $(FW_DIR)/test/fan_duty_test.cpp && $(OUT)/fan_duty_test
	@g++ -std=c++17 -Wall -Wextra -Werror -o $(OUT)/ota_policy_test $(FW_DIR)/test/ota_policy_test.cpp && $(OUT)/ota_policy_test

OTA_TEST_DIR := $(OUT)/ota_test
OTA_TEST_IMAGES := good_base:100:0 bad_selftest:101:1 bad_crash:102:2 bad_timeout:103:3 good_new:104:0

test-ota-rollback: flash-check ## OTA 回退的板上测试(覆盖 PORT 上开发板的引导程序和固件,保留 nvs 和 fctry)
	@mkdir -p $(OTA_TEST_DIR)
	@for spec in $(OTA_TEST_IMAGES); do \
	  name=$${spec%%:*}; rest=$${spec#*:}; ver=$${rest%%:*}; mode=$${rest#*:}; \
	  echo "编译测试镜像 $$name (版本 $$ver, 测试模式 $$mode)"; \
	  $(ENV); cd $(FW_DIR) && idf.py $(IDF_OUT) -DREAIR_TEST_PROJECT_VER=$$ver -DREAIR_OTA_TEST_MODE=$$mode build >/dev/null \
	    && cp $(FW_BUILD)/reair.bin $(OTA_TEST_DIR)/$$name.bin || exit 1; \
	done
	$(MAKE) build
	$(PY) -I $(FW_DIR)/test/ota_rollback/rollback_test.py --port $(PORT) --images $(OTA_TEST_DIR) --build $(FW_BUILD)

test-e2e: ## 两块板的端到端同步测试(需要两块已联网的板和 matter-server 转发,见 firmware/test/e2e/run_sync_tests.py)
	cd $(FW_DIR)/test/e2e && $(PY) -u run_sync_tests.py $(ARGS)

clean: ## 清除 out/(编译产物、生成的配置、外壳、PCB 生产文件)
	rm -rf $(OUT)
