cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

# 软件版本号取构建时的本地时间,格式 YYMMDDHH(ADR 0007);PROJECT_VER 是同一个数的字符串,
# 回退后据此读出被回退固件的版本号。make build 每次传入新值;直接用 idf.py 时取 cmake 配置时的时间
if(NOT DEFINED PROJECT_VER_NUMBER)
    string(TIMESTAMP PROJECT_VER_NUMBER "%y%m%d%H")
endif()
# REAIR_TEST_PROJECT_VER 仅供 OTA 回退测试单独指定版本字符串(不重编 Matter 库),发布构建必须为空
if(REAIR_TEST_PROJECT_VER)
    set(PROJECT_VER "${REAIR_TEST_PROJECT_VER}")
else()
    set(PROJECT_VER "${PROJECT_VER_NUMBER}")
endif()

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# sdkconfig.device 由 scripts/gen_commissioning.py 根据 config/ 生成(调试开关、VID/PID 等)。
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(DEVICE_SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/../out/sdkconfig.device")
if(NOT EXISTS "${DEVICE_SDKCONFIG}")
    message(FATAL_ERROR "缺少 out/sdkconfig.device,请先运行 scripts/gen_commissioning.py(或 make config)")
endif()
list(APPEND SDKCONFIG_DEFAULTS "${DEVICE_SDKCONFIG}")

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    ${extra_components_dirs_append})

project(reair)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
