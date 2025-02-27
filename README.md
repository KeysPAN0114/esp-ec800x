# EC800X Series Cat.1 AT Modem

This is a component for the Quec EC800x Cat.1 Module.
This project is initially created for https://github.com/78/xiaozhi-esp32

## Features

- AT Command
- MQTT / MQTTS
- HTTP / HTTPS
- SSLTCP
- WebSocket

## Supported Modules

- EC801E

## Sample Code

```cpp
#include "esp_log.h"
#include "ec800AtModem.h"
#include "ec800slTransport.h"
#include "ec800Http.h"
#include "ec800Mqtt.h"

static const char *TAG = "ec800";

void TestHttp(EC800AtModem& modem) {
    ESP_LOGI(TAG, "Starting HTTP test");

    EC800Http http(modem);
    http.SetHeader("User-Agent", "Xiaozhi/1.0.0");
    http.Open("GET", "https://xiaozhi.me/");
    
    // print body length & body
    ESP_LOGI(TAG, "Response body: %zu bytes", http.GetBodyLength());
    ESP_LOGI(TAG, "Response body: %s", http.GetBody().c_str());
    http.Close();
}

void TestMqtt(EC800AtModem& modem) {
    ESP_LOGI(TAG, "Starting MQTT test");

    EC800Mqtt mqtt(modem, 0);
    if (!mqtt.Connect("broker.emqx.io", 1883, "emqx", "public", "")) {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker");
        return;
    }
    mqtt.OnMessage([](const std::string& topic, const std::string& payload) {
        ESP_LOGI(TAG, "Received message: %s, %s", topic.c_str(), payload.c_str());
    });
    mqtt.Subscribe("test/clientid/event");
    mqtt.Publish("test", "Hello, MQTT!");
    vTaskDelay(pdMS_TO_TICKS(5000));
    mqtt.Disconnect();
}

void TestWebSocket(EC800AtModem& modem) {
    ESP_LOGI(TAG, "Starting WebSocket test");

    WebSocket ws(new EC800SslTransport(modem, 0));
    ws.SetHeader("Protocol-Version", "2");

    ws.OnConnected([]() {
        ESP_LOGI(TAG, "Connected to server");
    });

    ws.OnData([](const char* data, size_t length, bool binary) {
        ESP_LOGI(TAG, "Received data: %.*s", length, data);
    });

    ws.OnDisconnected([]() {
        ESP_LOGI(TAG, "Disconnected from server");
    });

    ws.OnError([](int error) {
        ESP_LOGE(TAG, "WebSocket error: %d", error);
    });

    if (!ws.Connect("wss://api.tenclass.net/xiaozhi/v1/")) {
        ESP_LOGE(TAG, "Failed to connect to server");
        return;
    }

    for (int i = 0; i < 10; i++) {
        ws.Send("{\"type\": \"hello\"}");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ws.Close();
}


extern "C" void app_main(void) {
    EC800AtModem modem(GPIO_NUM_13, GPIO_NUM_14, 2048);
    modem.SetDebug(true);
    modem.SetBaudRate(921600);

    modem.WaitForNetworkReady();

    // Print IP Address
    ESP_LOGI(TAG, "IP Address: %s", modem.ip_address().c_str());
    // Print IMEI, ICCID, Product ID, Carrier Name
    ESP_LOGI(TAG, "IMEI: %s", modem.GetImei().c_str());
    ESP_LOGI(TAG, "ICCID: %s", modem.GetIccid().c_str());
    ESP_LOGI(TAG, "Product ID: %s", modem.GetModuleName().c_str());
    ESP_LOGI(TAG, "Carrier Name: %s", modem.GetCarrierName().c_str());
    // Print CSQ
    ESP_LOGI(TAG, "CSQ: %d", modem.GetCsq());
    
    TestMqtt(modem);
    TestHttp(modem);
    TestWebSocket(modem);
}

```

## Author

- Terrence (terrence@tenclass.com)
