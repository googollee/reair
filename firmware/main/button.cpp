#include "button.h"
#include "ota.h"
#include "button_logic.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <esp_matter.h>

#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

namespace reair {
namespace button {
namespace {

const char *TAG = "button";

constexpr gpio_num_t kGpio = static_cast<gpio_num_t>(CONFIG_REAIR_BUTTON_GPIO);
constexpr uint32_t kDebounceMs = 50;
constexpr int64_t kLongPressUs = INT64_C(5000000);
constexpr uint32_t kCommissioningWindowSeconds = 15 * 60;
constexpr UBaseType_t kQueueDepth = 16;

// 按键状态只在 button_task 中读写。中断和定时器回调只往队列里放事件,不碰共享状态
enum class Event : uint8_t {
    edge,                // 引脚电平变化,尚未去抖
    long_press_expired,  // 长按计时到期
};

QueueHandle_t queue;
esp_timer_handle_t long_press_timer;

void open_commissioning_window(intptr_t)
{
    auto &manager = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (manager.IsCommissioningWindowOpen()) {
        ESP_LOGI(TAG, "Short press: commissioning window already open");
        return;
    }
    ESP_LOGI(TAG, "Short press: opening commissioning window");
    CHIP_ERROR err = manager.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(kCommissioningWindowSeconds));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void factory_reset(intptr_t)
{
    ESP_LOGW(TAG, "Long press released: factory reset");
    reair::ota::clear_failed_record();
    // 先清 esp-matter 保存的属性(角色、风向等),再做 Matter 的出厂复位并重启
    esp_err_t err = esp_matter::factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_matter factory reset failed: %d", err);
    }
}

void schedule(const char *what, chip::DeviceLayer::AsyncWorkFunct work)
{
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(work, 0);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to schedule %s: %" CHIP_ERROR_FORMAT, what, err.Format());
    }
}

void on_long_press_timer(void *)
{
    Event event = Event::long_press_expired;
    xQueueSend(queue, &event, 0);
}

void IRAM_ATTR on_edge(void *)
{
    Event event = Event::edge;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(queue, &event, &woken);
    portYIELD_FROM_ISR(woken);
}

bool is_down()
{
    return gpio_get_level(kGpio) == 0;
}

void perform(Action action)
{
    switch (action) {
    case Action::none:
        break;
    case Action::start_long_press_timer:
        esp_timer_stop(long_press_timer);
        esp_timer_start_once(long_press_timer, kLongPressUs);
        break;
    case Action::short_press:
        esp_timer_stop(long_press_timer);
        schedule("commissioning window", open_commissioning_window);
        break;
    case Action::factory_reset:
        schedule("factory reset", factory_reset);
        break;
    }
}

void expire_long_press(Logic &logic)
{
    logic.on_long_press_expired(is_down());
    if (logic.long_pressed()) {
        ESP_LOGW(TAG, "Long press reached, release to factory reset");
    }
}

void button_task(void *)
{
    Logic logic;
    Event event;
    while (true) {
        if (xQueueReceive(queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (event == Event::long_press_expired) {
            expire_long_press(logic);
            continue;
        }

        // 等电平稳定后只看此刻的电平,不看边沿,抖动不会漏掉最终状态。
        // 等待期间排队的事件一并取出,其中的长按到期不能丢
        vTaskDelay(pdMS_TO_TICKS(kDebounceMs));
        bool expired_while_debouncing = false;
        Event drained;
        while (xQueueReceive(queue, &drained, 0) == pdTRUE) {
            expired_while_debouncing |= (drained == Event::long_press_expired);
        }

        perform(logic.on_settled_level(is_down()));
        if (expired_while_debouncing) {
            // 重新读电平:若上面已经处理成松开,这里不会误判为长按
            expire_long_press(logic);
        }
    }
}

}  // namespace

esp_err_t start()
{
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << kGpio;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_ANYEDGE;
    esp_err_t gpio_err = gpio_config(&config);
    if (gpio_err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(gpio_err));
        return gpio_err;
    }

    queue = xQueueCreate(kQueueDepth, sizeof(Event));
    if (queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create event queue, button disabled");
        return ESP_ERR_NO_MEM;
    }

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &on_long_press_timer;
    timer_args.name = "btn_long_press";
    if (esp_timer_create(&timer_args, &long_press_timer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create long press timer, button disabled");
        vQueueDelete(queue);
        return ESP_ERR_NO_MEM;
    }

    // INVALID_STATE 表示其他组件已经安装过
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        esp_timer_delete(long_press_timer);
        vQueueDelete(queue);
        return err;
    }
    err = gpio_isr_handler_add(kGpio, on_edge, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(err));
        esp_timer_delete(long_press_timer);
        vQueueDelete(queue);
        return err;
    }

    if (xTaskCreate(button_task, "button", 3072, nullptr, 1, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task, button disabled");
        gpio_isr_handler_remove(kGpio);
        esp_timer_delete(long_press_timer);
        vQueueDelete(queue);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Button on GPIO%d: short press = open commissioning window, long press (>=%llds) = factory reset",
             static_cast<int>(kGpio), static_cast<long long>(kLongPressUs / 1000000));
    return ESP_OK;
}

}  // namespace button
}  // namespace reair
