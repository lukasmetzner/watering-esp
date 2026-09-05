#include "pump.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define GPIO_OUTPUT_PIN 26

#define RELAY_ON_LEVEL  0
#define RELAY_OFF_LEVEL 1

/* Safety: a bare "ON" runs for this long, and no run may exceed the max.
 * A crash or lost connection can then never leave the pump running forever. */
#define DEFAULT_ON_SECONDS 5
#define MAX_ON_SECONDS     30

static const char *TAG = "pump";

static relay_state_cb_t s_state_cb;
static esp_timer_handle_t s_off_timer;
static bool s_relay_on;

bool relay_is_on(void) {
    return s_relay_on;
}

void relay_set_state_cb(relay_state_cb_t cb) {
    s_state_cb = cb;
}

static void relay_apply(bool on) {
    gpio_set_level(GPIO_OUTPUT_PIN, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
    bool changed = (on != s_relay_on);
    s_relay_on = on;
    ESP_LOGI(TAG, "relay %s (gpio %d)", on ? "ON" : "OFF", GPIO_OUTPUT_PIN);
    if (changed && s_state_cb) {
        s_state_cb(on);
    }
}

static void off_timer_cb(void *arg) {
    ESP_LOGI(TAG, "auto-off timer expired");
    relay_apply(false);
}

static void relay_on_for(int seconds) {
    esp_timer_stop(s_off_timer); /* no-op if not running */
    relay_apply(true);
    if (seconds > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(
            s_off_timer,
            (int64_t)seconds * 1000000)
        );
        ESP_LOGI(TAG, "auto-off in %d s", seconds);
    }
}

static void relay_off(void) {
    esp_timer_stop(s_off_timer); /* no-op if not running */
    relay_apply(false);
}

void pump_init(void) {
    gpio_reset_pin(GPIO_OUTPUT_PIN);
    // Avoid ON level during boot
    gpio_set_level(GPIO_OUTPUT_PIN, RELAY_OFF_LEVEL);
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_OUTPUT_PIN, GPIO_MODE_OUTPUT));

    const esp_timer_create_args_t timer_args = {
        .callback = off_timer_cb,
        .name = "relay_off",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_off_timer));

    relay_off();
}

/* Accepts: "ON", "ON:<seconds>", "OFF" (case-insensitive, surrounding
 * whitespace ignored). Returns 0 on a recognised command, -1 otherwise. */
int handle_command(int cmd_len, char *cmd_data) {
    while (cmd_len > 0 && isspace((unsigned char)cmd_data[0])) {
        cmd_data++;
        cmd_len--;
    }
    while (cmd_len > 0 && isspace((unsigned char)cmd_data[cmd_len - 1])) {
        cmd_len--;
    }

    char buf[16];
    if (cmd_len < 1 || cmd_len >= (int)sizeof(buf)) {
        ESP_LOGW(TAG, "bad command length %d", cmd_len);
        return -1;
    }
    memcpy(buf, cmd_data, cmd_len);
    buf[cmd_len] = '\0';

    if (strcasecmp(buf, "OFF") == 0) {
        relay_off();
        return 0;
    }

    if (strcasecmp(buf, "ON") == 0) {
        relay_on_for(DEFAULT_ON_SECONDS);
        return 0;
    }

    if (strncasecmp(buf, "ON:", 3) == 0) {
        char *end = NULL;
        long secs = strtol(buf + 3, &end, 10);
        if (end == buf + 3 || *end != '\0' || secs <= 0) {
            ESP_LOGW(TAG, "bad duration in '%s'", buf);
            return -1;
        }
        if (secs > MAX_ON_SECONDS) {
            ESP_LOGW(TAG, "clamping %ld s to %d s", secs, MAX_ON_SECONDS);
            secs = MAX_ON_SECONDS;
        }
        relay_on_for((int)secs);
        return 0;
    }

    ESP_LOGW(TAG, "unknown cmd '%s'", buf);
    return -1;
}
