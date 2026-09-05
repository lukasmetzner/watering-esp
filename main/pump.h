#pragma once

#include <stdbool.h>

/* Called whenever the relay changes state (from a command or the auto-off
 * timer). Runs in the caller's context - the MQTT event task or the esp_timer
 * task - so keep it short and only enqueue work. */
typedef void (*relay_state_cb_t)(bool on);

void pump_init(void);
void relay_set_state_cb(relay_state_cb_t cb);
bool relay_is_on(void);
int handle_command(int cmd_len, char *cmd_data);
