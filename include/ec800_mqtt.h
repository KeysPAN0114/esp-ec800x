#ifndef EC800_MQTT_H
#define EC800_MQTT_H

#include "mqtt.h"

#include "ec800_at_modem.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <string>
#include <functional>

#define MQTT_CONNECT_TIMEOUT_MS 10000

#define MQTT_INITIALIZED_EVENT BIT0
#define MQTT_CONNECTED_EVENT BIT1
#define MQTT_DISCONNECTED_EVENT BIT2

class EC800Mqtt : public Mqtt {
public:
    EC800Mqtt(EC800AtModem& modem, int mqtt_id);
    ~EC800Mqtt();

    bool Connect(const std::string broker_address, int broker_port, const std::string client_id, const std::string username, const std::string password);
    void Disconnect();
    bool Publish(const std::string topic, const std::string payload, int qos = 0);
    bool Subscribe(const std::string topic, int qos = 0);
    bool Unsubscribe(const std::string topic);
    bool IsConnected();

private:
    EC800AtModem& modem_;
    int mqtt_id_;
    bool connected_ = false;
    EventGroupHandle_t event_group_handle_;
    std::string broker_address_;
    int broker_port_ = 1883;
    std::string client_id_;
    std::string username_;
    std::string password_;
    std::string message_payload_;

    std::list<EcCommandResponseCallback>::iterator command_callback_it_;

    std::string ErrorToString(int error_code);
};

#endif