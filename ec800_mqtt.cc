#include "ec800_mqtt.h"
#include <esp_log.h>
static const char *TAG = "EC800Mqtt";
#define MQTT_OPENED_EVENT BIT3
EC800Mqtt::EC800Mqtt(EC800AtModem& modem, int mqtt_id) : modem_(modem), mqtt_id_(mqtt_id) {
    event_group_handle_ = xEventGroupCreate();

    command_callback_it_ = modem_.RegisterCommandResponseCallback([this](const std::string command, const std::vector<AtArgumentValueEC>& arguments) {
        if (command == "MQTTURC" && arguments.size() >= 2) {
            if (arguments[1].int_value == mqtt_id_) {
                auto type = arguments[0].string_value;
                if (type == "conn") {
                    if (arguments[2].int_value == 0) {
                        xEventGroupSetBits(event_group_handle_, MQTT_CONNECTED_EVENT);
                    } else {
                        if (connected_) {
                            connected_ = false;
                            if (on_disconnected_callback_) {
                                on_disconnected_callback_();
                            }
                        }
                        xEventGroupSetBits(event_group_handle_, MQTT_DISCONNECTED_EVENT);
                    }
                    ESP_LOGI(TAG, "MQTT connection state: %s", ErrorToString(arguments[2].int_value).c_str());
                } else if (type == "suback") {
                } else if (type == "publish" && arguments.size() >= 7) {
                    auto topic = arguments[3].string_value;
                    if (arguments[4].int_value == arguments[5].int_value) {
                        if (on_message_callback_) {
                            on_message_callback_(topic, modem_.DecodeHex(arguments[6].string_value));
                        }
                    } else {
                        message_payload_.append(modem_.DecodeHex(arguments[6].string_value));
                        if (message_payload_.size() >= arguments[4].int_value && on_message_callback_) {
                            on_message_callback_(topic, message_payload_);
                            message_payload_.clear();
                        }
                    }
                } else {
                    ESP_LOGI(TAG, "unhandled MQTT event: %s", type.c_str());
                }
            }
        } else if (command == "QMTCONN" && arguments.size() == 2) {
            connected_ = arguments[1].int_value != 4;
            xEventGroupSetBits(event_group_handle_, MQTT_INITIALIZED_EVENT);
        } else if (command == "QMTCONN" && arguments.size() >= 3) {
            if (arguments[0].int_value == mqtt_id_) {
                if (arguments[1].int_value == 0 && arguments[2].int_value == 0) {
                    ESP_LOGI(TAG, "MQTT connection state: %s", ErrorToString(arguments[2].int_value).c_str());
                    xEventGroupSetBits(event_group_handle_, MQTT_CONNECTED_EVENT);
                } else {
                    if (connected_) {
                        connected_ = false;
                        if (on_disconnected_callback_) {
                            on_disconnected_callback_();
                        }
                    }
                    xEventGroupSetBits(event_group_handle_, MQTT_DISCONNECTED_EVENT);
                }
            }
        } else if (command == "QMTOPEN") {
            if (arguments[0].int_value == mqtt_id_) {
                if (arguments[1].int_value == 0)
                    xEventGroupSetBits(event_group_handle_, MQTT_OPENED_EVENT);
                }
                ESP_LOGI(TAG, "MQTT open state: %s", ErrorToString(arguments[1].int_value).c_str());
            }
    });
}

EC800Mqtt::~EC800Mqtt() {
    modem_.UnregisterCommandResponseCallback(command_callback_it_);
    vEventGroupDelete(event_group_handle_);
}

bool EC800Mqtt::Connect(const std::string broker_address, int broker_port, const std::string client_id, const std::string username, const std::string password) {
    broker_address_ = broker_address;
    broker_port_ = broker_port;
    client_id_ = client_id;
    username_ = username;
    password_ = password;

    EventBits_t bits;
    // if (IsConnected()) {
    //     // 断开之前的连接
    //     Disconnect();
    //     bits = xEventGroupWaitBits(event_group_handle_, MQTT_DISCONNECTED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS));
    //     if (!(bits & MQTT_DISCONNECTED_EVENT)) {
    //         ESP_LOGE(TAG, "Failed to disconnect from previous connection");
    //         return false;
    //     }
    // }

    if (broker_port_ == 8883) {
        modem_.Command(std::string("AT+QMTCFG=\"SSL\",") + std::to_string(mqtt_id_) + ",1,1",3000);
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    // Set clean session
    // if (!modem_.Command(std::string("AT+MQTTCFG=\"clean\",") + std::to_string(mqtt_id_) + ",1")) {
    //     ESP_LOGE(TAG, "Failed to set MQTT clean session");
    //     return false;
    // }

    modem_.Command(std::string("AT+QMTCFG=\"version\",") + std::to_string(mqtt_id_) + ",4",3000);

    modem_.Command(std::string("AT+QMTCFG=\"aliauth\",") + std::to_string(mqtt_id_),3000);

    // Set keep alive
    modem_.Command(std::string("AT+QMTCFG=\"qmtping\",") + std::to_string(mqtt_id_) + "," + std::to_string(keep_alive_seconds_),3000);

    modem_.Command("AT+QMTOPEN=" + std::to_string(mqtt_id_) + ",\"" + broker_address_ + "\"," + std::to_string(broker_port_),3000);
    xEventGroupWaitBits(event_group_handle_, MQTT_OPENED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS));
    // 创建MQTT连接
    modem_.Command("AT+QMTCONN=" + std::to_string(mqtt_id_) + ",\"" + client_id_ + "\",\"" + username_ + "\",\"" + password_ + "\"",3000);
    // if (!modem_.Command(command)) {
    //     ESP_LOGE(TAG, "Failed to create MQTT connection");
    //     return false;
    // }

    // 等待连接完成
    bits = xEventGroupWaitBits(event_group_handle_, MQTT_CONNECTED_EVENT | MQTT_DISCONNECTED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS));
    if (!(bits & MQTT_CONNECTED_EVENT)) {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker");
        return false;
    }

    // Set HEX encoding
    modem_.Command("AT+QMTCFG=\"dataformat\"," + std::to_string(mqtt_id_) + ",1,1");

    connected_ = true;
    if (on_connected_callback_) {
        on_connected_callback_();
    }
    return true;
}

bool EC800Mqtt::IsConnected() {
    // 检查这个 id 是否已经连接
    modem_.Command(std::string("AT+QMTCONN=") + std::to_string(mqtt_id_));
    auto bits = xEventGroupWaitBits(event_group_handle_, MQTT_INITIALIZED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS));
    if (!(bits & MQTT_INITIALIZED_EVENT)) {
        ESP_LOGE(TAG, "Failed to initialize MQTT connection");
        return false;
    }
    return connected_;
}

void EC800Mqtt::Disconnect() {
    if (!connected_) {
        return;
    }
    modem_.Command(std::string("AT+QMTDISC=") + std::to_string(mqtt_id_));
}

bool EC800Mqtt::Publish(const std::string topic, const std::string payload, int qos) {
    if (!connected_) {
        return false;
    }
    std::string command = "AT+QMTPUBEX=" + std::to_string(mqtt_id_) + "," + std::to_string(mqtt_id_) + ",";
    command += std::to_string(qos) + ",0," + "\"" + topic + "\",";
    command += std::to_string(payload.size());
    modem_.Command(command);;
    return modem_.Command(modem_.EncodeHex(payload));
}

bool EC800Mqtt::Subscribe(const std::string topic, int qos) {
    if (!connected_) {
        return false;
    }
    std::string command = "AT+MQTTSUB=" + std::to_string(mqtt_id_) + "," + std::to_string(mqtt_id_) + ",\"" + topic + "\"," + std::to_string(qos);
    return modem_.Command(command);
}

bool EC800Mqtt::Unsubscribe(const std::string topic) {
    if (!connected_) {
        return false;
    }
    std::string command = "AT+MQTTUNSUB=" + std::to_string(mqtt_id_) + "," + std::to_string(mqtt_id_) + ",\"" + topic + "\"";
    return modem_.Command(command);
}

std::string EC800Mqtt::ErrorToString(int error_code) {
    switch (error_code) {
        case 0:
            return "MQTT连接打开/连接成功";
        case 1:
            return "MQTT初始化";
        case 2:
            return "MQTT正在连接";
        case 3:
            return "MQTT已经连接成功";
        case 4:
            return "MQTT正在断开连接";
        default:
            return "未知错误";
    }
}
