idf_component_register(SRC_DIRS          "."
                       PRIV_INCLUDE_DIRS  "." "${ESP_MATTER_PATH}/examples/common/utils")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

# 仅测试用:REAIR_OTA_TEST_MODE 让新固件在 OTA 自检时故意失败(见 ota.cpp),发布构建必须为 0
if(NOT DEFINED REAIR_OTA_TEST_MODE)
    set(REAIR_OTA_TEST_MODE 0)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE "REAIR_OTA_TEST_MODE=${REAIR_OTA_TEST_MODE}")
