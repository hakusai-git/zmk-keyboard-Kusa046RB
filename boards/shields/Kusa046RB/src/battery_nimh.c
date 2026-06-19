#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

/* XIAO BLEの内蔵LED */
#define LED_NODE DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS(LED_NODE, okay)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
#endif

/* 📊 cormoran氏のNiMH 1本用電圧曲線 (0%〜100%、10%刻みの11ステップ) */
static const uint32_t nimh_curve_mv[] = {1100, 1150, 1200, 1220, 1240, 1260,
                                         1280, 1300, 1320, 1350, 1400};

/**
 * 実電圧(mV)から、曲線テーブルを使ってパーセントを計算する
 */
uint8_t get_nimh_battery_percent(uint32_t real_mv) {
    if (real_mv <= nimh_curve_mv[0]) return 0;
    if (real_mv >= nimh_curve_mv[10]) return 100;

    for (int i = 0; i < 10; i++) {
        if (real_mv >= nimh_curve_mv[i] && real_mv <= nimh_curve_mv[i + 1]) {
            uint32_t range = nimh_curve_mv[i + 1] - nimh_curve_mv[i];
            uint32_t offset = real_mv - nimh_curve_mv[i];
            // 線形補間してパーセントを返す
            return (i * 10) + ((offset * 10) / range);
        }
    }
    return 100;
}

void battery_nimh_thread(void) {
    /* デバイスツリーから vbatt ノード（電圧計算をしてくれるドライバ）を取得 */
    const struct device* vbatt_dev = DEVICE_DT_GET(DT_NODELABEL(vbatt_nimh));
#if DT_NODE_HAS_STATUS(LED_NODE, okay)
    if (device_is_ready(led.port)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }
#endif

    if (!device_is_ready(vbatt_dev)) {
        return; /* センサーの準備ができていなければ終了 */
    }

    while (1) {
        /* 1. ZMKのドライバに電圧を測定させる */
        if (sensor_sample_fetch(vbatt_dev) == 0) {
            struct sensor_value voltage;

            /* 2.
             * 分圧抵抗の計算（1M/470k）がすでに完了した「実際の電池電圧」を取得
             */
            sensor_channel_get(vbatt_dev, SENSOR_CHAN_VOLTAGE, &voltage);

            /* ボルトとマイクロボルトを、ミリボルト(mV)に統合 */
            uint32_t real_mv = (voltage.val1 * 1000) + (voltage.val2 / 1000);

            /* 3. NiMHの曲線に当てはめてパーセントを計算 */
            uint8_t percent = get_nimh_battery_percent(real_mv);

            /* 4. ZMKシステム（Bluetooth等）へパーセントを強制通知 */
            raise_zmk_battery_state_changed(
                (struct zmk_battery_state_changed){.state_of_charge = percent});

            /* 5. LED制御: 20%以下で点灯（警告） */
#if DT_NODE_HAS_STATUS(LED_NODE, okay)
            if (device_is_ready(led.port)) {
                gpio_pin_set_dt(&led, percent <= 20 ? 1 : 0);
            }
#endif
        }
        /* 60秒に1回測定して電力を節約 */
        k_sleep(K_SECONDS(60));
    }
}

// バックグラウンドで動くスレッドとして起動
K_THREAD_DEFINE(battery_nimh_tid, 1024, battery_nimh_thread, NULL, NULL, NULL,
                7, 0, 0);