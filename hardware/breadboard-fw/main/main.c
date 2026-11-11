// 面包板验证固件:通过 USB 串口命令行控制 GPIO4 上的 PWM,见 ../../breadboard.md

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PWM_GPIO 4
#define LED_GPIO 8  // 开发板板载蓝色 LED
#define PWM_RES LEDC_TIMER_12_BIT
#define PWM_MAX ((1 << 12) - 1)
#define FREQ_MIN 2000
#define FREQ_MAX 5000

static double line_duty = 50.0;  // 风扇 PWM 线上的占空比,单位 %
static uint32_t freq_hz = 3000;
static bool invert = true;       // MOSFET 反相:GPIO 高 = 风扇 PWM 线低
static volatile bool sweep_stop;
static volatile bool sweeping;

static void apply(void)
{
    double gpio_duty = invert ? 100.0 - line_duty : line_duty;
    uint32_t raw = (uint32_t)lround(gpio_duty / 100.0 * PWM_MAX);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, raw);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void set_duty(double pct)
{
    line_duty = pct;
    apply();
}

static bool parse_double(const char *s, double lo, double hi, double *out)
{
    char *end;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || v < lo || v > hi) {
        printf("参数 '%s' 无效,范围 %g~%g\n", s, lo, hi);
        return false;
    }
    *out = v;
    return true;
}

static int cmd_duty(int argc, char **argv)
{
    double v;
    if (argc != 2 || !parse_double(argv[1], 0, 100, &v)) {
        printf("用法: duty <0-100>\n");
        return 1;
    }
    sweep_stop = true;
    set_duty(v);
    printf("紫线占空比 %.2f%%\n", line_duty);
    return 0;
}

static int cmd_freq(int argc, char **argv)
{
    double v;
    if (argc != 2 || !parse_double(argv[1], FREQ_MIN, FREQ_MAX, &v)) {
        printf("用法: freq <%d-%d>\n", FREQ_MIN, FREQ_MAX);
        return 1;
    }
    freq_hz = (uint32_t)lround(v);
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq_hz);
    apply();
    printf("频率 %u Hz\n", (unsigned)freq_hz);
    return 0;
}

static int cmd_stop(int argc, char **argv)
{
    sweep_stop = true;
    set_duty(50.0);
    printf("已中止扫描,紫线占空比 50%%\n");
    return 0;
}

static int cmd_invert(int argc, char **argv)
{
    if (argc != 2 || (argv[1][0] != '0' && argv[1][0] != '1') || argv[1][1] != '\0') {
        printf("用法: invert <0|1>\n");
        return 1;
    }
    invert = argv[1][0] == '1';
    apply();
    printf("极性 invert=%d,紫线占空比 %.2f%%\n", invert, line_duty);
    return 0;
}

static int cmd_led(int argc, char **argv)
{
    if (argc != 2 || (argv[1][0] != '0' && argv[1][0] != '1') || argv[1][1] != '\0') {
        printf("用法: led <0|1>\n");
        return 1;
    }
    gpio_set_level(LED_GPIO, argv[1][0] - '0');
    printf("GPIO%d = %c\n", LED_GPIO, argv[1][0]);
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    printf("紫线占空比 %.2f%%  频率 %u Hz  invert=%d  扫描%s\n", line_duty, (unsigned)freq_hz, invert,
           sweeping ? "中" : "未运行");
    return 0;
}

typedef struct {
    double from, to, step;
    uint32_t ms;
} sweep_args_t;

static void sweep_task(void *arg)
{
    sweep_args_t a = *(sweep_args_t *)arg;
    free(arg);
    double dir = a.to >= a.from ? 1.0 : -1.0;
    int n = (int)floor(fabs(a.to - a.from) / a.step + 1e-9);
    for (int i = 0; i <= n && !sweep_stop; i++) {
        set_duty(a.from + dir * a.step * i);
        printf("%.2f%%\n", line_duty);
        vTaskDelay(pdMS_TO_TICKS(a.ms));
    }
    printf("扫描%s\n", sweep_stop ? "已中止" : "结束");
    sweeping = false;
    vTaskDelete(NULL);
}

static int cmd_sweep(int argc, char **argv)
{
    double from, to, step, ms;
    if (argc != 5 || !parse_double(argv[1], 0, 100, &from) || !parse_double(argv[2], 0, 100, &to) ||
        !parse_double(argv[3], 0.01, 100, &step) || !parse_double(argv[4], 50, 600000, &ms)) {
        printf("用法: sweep <起%%> <止%%> <步长%%> <每步毫秒>\n");
        return 1;
    }
    if (sweeping) {
        printf("扫描正在运行,先执行 stop\n");
        return 1;
    }
    sweep_args_t *a = malloc(sizeof(*a));
    if (a == NULL) {
        return 1;
    }
    *a = (sweep_args_t){from, to, step, (uint32_t)ms};
    sweep_stop = false;
    sweeping = true;
    if (xTaskCreate(sweep_task, "sweep", 4096, a, 5, NULL) != pdPASS) {
        free(a);
        sweeping = false;
        printf("创建扫描任务失败\n");
        return 1;
    }
    return 0;
}

static void register_cmd(const char *name, const char *help, const char *hint, esp_console_cmd_func_t func)
{
    const esp_console_cmd_t cmd = {.command = name, .help = help, .hint = hint, .func = func};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void app_main(void)
{
    // 尽早输出 50%(停机点)。在此之前 MOSFET 截止,风扇 PWM 线被上拉,相当于 100%
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = PWM_RES,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t ch = {
        .gpio_num = PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    apply();

    gpio_set_level(LED_GPIO, 1);  // LED 低电平点亮,启动后保持熄灭
    ESP_ERROR_CHECK(gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT));

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "fan> ";
    esp_console_dev_usb_serial_jtag_config_t dev_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dev_config, &repl_config, &repl));

    register_cmd("duty", "设置紫线占空比(%)", "<0-100>", cmd_duty);
    register_cmd("freq", "设置 PWM 频率(Hz)", "<2000-5000>", cmd_freq);
    register_cmd("stop", "中止扫描并设为 50%", NULL, cmd_stop);
    register_cmd("sweep", "扫描占空比", "<起> <止> <步长> <毫秒>", cmd_sweep);
    register_cmd("invert", "设置 MOSFET 反相补偿", "<0|1>", cmd_invert);
    register_cmd("led", "设置板载 LED 引脚电平", "<0|1>", cmd_led);
    register_cmd("status", "显示当前状态", NULL, cmd_status);
    ESP_ERROR_CHECK(esp_console_register_help_command());

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
