#include "mqtt.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "moisture.h"
#include "mqtt_client.h"
#include "pump.h"

static const char* TAG = "mqtt5";

/* Topic layout, per device (id = the STA MAC as 12 hex chars):
 *   watering/<id>/pump/cmd    <- subscribe: "ON" | "ON:<seconds>" | "OFF"
 *   watering/<id>/pump/state  -> publish (retained): "ON" | "OFF"
 *   watering/<id>/status      -> publish (retained): "online" | "offline" (LWT)
 */
static char topic_cmd[48];
static char topic_state[48];
static char topic_status[48];
static char topic_moisture[48];

static esp_mqtt_client_handle_t s_client;

static void build_topics(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char id[13];
    snprintf(
        id,
        sizeof(id),
        "%02x%02x%02x%02x%02x%02x",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]);
    snprintf(topic_cmd, sizeof(topic_cmd), "watering/%s/pump/cmd", id);
    snprintf(topic_state, sizeof(topic_state), "watering/%s/pump/state", id);
    snprintf(topic_status, sizeof(topic_status), "watering/%s/status", id);
    snprintf(
        topic_moisture,
        sizeof(topic_moisture),
        "watering/%s/moisture",
        id);
    ESP_LOGI(TAG, "device id %s", id);
}

static void publish_state(bool on) {
    if (s_client) {
        esp_mqtt_client_publish(
            s_client,
            topic_state,
            on ? "ON" : "OFF",
            0,
            1,
            true);
    }
}

static esp_mqtt5_publish_property_config_t publish_property = {
    .payload_format_indicator = 1,
    .message_expiry_interval = 1000,
    .topic_alias = 0,
    .response_topic = "/topic/test/response",
    .correlation_data = "123456",
    .correlation_data_len = 6,
};

static esp_mqtt5_subscribe_property_config_t subscribe_property = {
    .subscribe_id = 25555,
    .no_local_flag = false,
    .retain_as_published_flag = false,
    .retain_handle = 0,
    .is_share_subscribe = true,
    .share_name = "group1",
};

static esp_mqtt5_subscribe_property_config_t subscribe1_property = {
    .subscribe_id = 25555,
    .no_local_flag = true,
    .retain_as_published_flag = false,
    .retain_handle = 0,
};

static esp_mqtt5_unsubscribe_property_config_t unsubscribe_property = {
    .is_share_subscribe = true,
    .share_name = "group1",
};

static esp_mqtt5_disconnect_property_config_t disconnect_property = {
    .session_expiry_interval = 60,
    .disconnect_reason = 0,
};

static void log_error_if_nonzero(const char* message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void moisture_publish_task(void* pvParameters) {
    while (1) {
        char data[20];
        int moisture = read_moisture_sensor();
        snprintf(data, 20, "%d", moisture);
        int msg_id =
            esp_mqtt_client_publish(s_client, topic_moisture, data, 0, 0, 0);
        if (msg_id == 0)
            ESP_LOGI(TAG, "Sent Data: %d", moisture);
        else
            ESP_LOGI(TAG, "Error msg_id:%d while sending data", msg_id);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void mqtt5_event_handler(
    void* handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void* event_data) {
    ESP_LOGD(
        TAG,
        "Event dispatched from event loop base=%s, event_id=%" PRIi32,
        base,
        event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    ESP_LOGD(
        TAG,
        "free heap size is %" PRIu32 ", minimum %" PRIu32,
        esp_get_free_heap_size(),
        esp_get_minimum_free_heap_size());
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_publish(client, topic_status, "online", 0, 1, true);
            publish_state(relay_is_on());
            esp_mqtt5_client_set_subscribe_property(
                client,
                &subscribe1_property);
            msg_id = esp_mqtt_client_subscribe(client, topic_cmd, 1);
            ESP_LOGI(TAG, "subscribed to %s, msg_id=%d", topic_cmd, msg_id);
            xTaskCreate(
                moisture_publish_task,
                "mqtt_publish_task",
                4096,
                NULL,
                5,
                NULL);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(
                TAG,
                "MQTT_EVENT_SUBSCRIBED, msg_id=%d, reason code=0x%02x ",
                event->msg_id,
                (uint8_t)*event->data);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            esp_mqtt5_client_set_disconnect_property(
                client,
                &disconnect_property);
            esp_mqtt_client_disconnect(client);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(
                TAG,
                "payload_format_indicator is %d",
                event->property->payload_format_indicator);
            ESP_LOGI(
                TAG,
                "response_topic is %.*s",
                event->property->response_topic_len,
                event->property->response_topic);
            ESP_LOGI(
                TAG,
                "correlation_data is %.*s",
                event->property->correlation_data_len,
                event->property->correlation_data);
            ESP_LOGI(
                TAG,
                "content_type is %.*s",
                event->property->content_type_len,
                event->property->content_type);
            ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
            // Ignore fragmented payload
            if (event->current_data_offset != 0 ||
                event->data_len != event->total_data_len) {
                ESP_LOGW(
                    TAG,
                    "ignoring fragmented payload (offset=%d len=%d total=%d)",
                    event->current_data_offset,
                    event->data_len,
                    event->total_data_len);
                break;
            }
            if (event->topic_len == strlen(topic_cmd) &&
                strncmp(topic_cmd, event->topic, event->topic_len) == 0) {
                if (handle_command(event->data_len, event->data) != 0) {
                    ESP_LOGI(
                        TAG,
                        "Invalid command=%.*s",
                        event->data_len,
                        event->data);
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            ESP_LOGI(
                TAG,
                "MQTT5 return code is %d",
                event->error_handle->connect_return_code);
            if (event->error_handle->error_type ==
                MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero(
                    "reported from esp-tls",
                    event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero(
                    "reported from tls stack",
                    event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero(
                    "captured as transport's socket errno",
                    event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(
                    TAG,
                    "Last errno string (%s)",
                    strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

void mqtt_app_start(void) {
    build_topics();
    relay_set_state_cb(publish_state);

    esp_mqtt5_connection_property_config_t connect_property = {
        .session_expiry_interval = 10,
        .maximum_packet_size = 1024,
        .receive_maximum = 65535,
        .topic_alias_maximum = 2,
        .request_resp_info = true,
        .request_problem_info = true,
        .will_delay_interval = 10,
        .payload_format_indicator = true,
        .message_expiry_interval = 10,
        .response_topic = "/test/response",
        .correlation_data = "123456",
        .correlation_data_len = 6,
    };

    const esp_mqtt_client_config_t mqtt5_cfg = {
        .broker =
            {
                .address.uri = CONFIG_MQTT_BROKER_URI,
            },
        .session =
            {
                .protocol_ver = MQTT_PROTOCOL_V_5,
                .last_will =
                    {
                        .topic = topic_status,
                        .msg = "offline",
                        .msg_len = 7,
                        .qos = 1,
                        .retain = true,
                    },
            },
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt5_cfg);
    s_client = client;

    // esp_mqtt5_client_set_user_property(&connect_property.user_property,
    // user_property_arr, USE_PROPERTY_ARR_SIZE);
    // esp_mqtt5_client_set_user_property(&connect_property.will_user_property,
    // user_property_arr, USE_PROPERTY_ARR_SIZE);
    esp_mqtt5_client_set_connect_property(client, &connect_property);

    esp_mqtt_client_register_event(
        client,
        ESP_EVENT_ANY_ID,
        mqtt5_event_handler,
        NULL);
    esp_mqtt_client_start(client);
}
