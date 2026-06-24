#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

/* XIAO BLEの内蔵LED（青色など、適宜変更可能）を取得 */
#define LED_NODE DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS(LED_NODE, okay)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
#endif

/**
 * @brief ZMKがバッテリー残量を更新した瞬間に呼び出されるリスナー関数
 */
static int battery_led_listener_cb(const zmk_event_t* eh) {
    // イベントから現在のバッテリー残量（％）を取得
    const struct zmk_battery_state_changed* ev =
        as_zmk_battery_state_changed(eh);
    if (ev == NULL) return 0;

    uint8_t percent = ev->state_of_charge;

    // LEDデバイスが準備できていれば制御する
    if (device_is_ready(led.port)) {
        if (percent <= 20) {
            // 残量20%以下ならLEDを点灯（電池交換のサイン）
            gpio_pin_set_dt(&led, 1);
        } else {
            // 21%以上なら消灯して電力を節約
            gpio_pin_set_dt(&led, 0);
        }
    }
    return 0;
}

/* ZMKのイベントシステムにこの関数を登録する（バッテリー変化イベントを購読） */
ZMK_LISTENER(battery_led_listener, battery_led_listener_cb);
ZMK_SUBSCRIPTION(battery_led_listener, zmk_battery_state_changed);

/**
 * @brief キーボード起動時に一度だけ実行される初期化関数
 */
static int battery_led_init(const struct device* dev) {
    if (device_is_ready(led.port)) {
        // LEDピンを出力モードとして初期化（最初は消灯）
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }
    return 0;
}

// Zephyrのアプリケーション初期化フェーズで自動実行させる
SYS_INIT(battery_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);