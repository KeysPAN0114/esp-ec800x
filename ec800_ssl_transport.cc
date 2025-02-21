#include "ec800_ssl_transport.h"
#include <esp_log.h>
#include <cstring>

static const char *TAG = "EC800SslTransport";


EC800SslTransport::EC800SslTransport(EC800AtModem& modem, int tcp_id) : modem_(modem), tcp_id_(tcp_id) {
    event_group_handle_ = xEventGroupCreate();

    command_callback_it_ = modem_.RegisterCommandResponseCallback([this](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
        if (command == "QISTATE" && arguments.size() >= 2) {
            if (arguments[0].int_value == tcp_id_) {
                if (arguments[1].int_value == 3) {
                    connected_ = true;
                    xEventGroupClearBits(event_group_handle_, EC800_SSL_TRANSPORT_DISCONNECTED | EC800_SSL_TRANSPORT_ERROR);
                    xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_CONNECTED);
                } else {
                    connected_ = false;
                    xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_ERROR);
                }
            }
        } else if (command == "QISTATE" && arguments.size() == 1) {
            if (arguments[0].int_value == tcp_id_) {
                connected_ = false;
                xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_DISCONNECTED);
            }
        } else if (command == "QISEND" && arguments.size() >= 2) {
            xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_SEND_COMPLETE);
        } else if (command == "QIURC" && arguments.size() >= 2) {
                if (arguments[0].string_value == "recv") {
                    std::lock_guard<std::mutex> lock(mutex_);
                    xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_RECEIVE);
                } else if (arguments[0].string_value == "closed") {
                    connected_ = false;
                    xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_DISCONNECTED);
                } else {
                    ESP_LOGE(TAG, "Unknown MIPURC command: %s", arguments[0].string_value.c_str());
                }
                modem_.Command(std::string("AT+QIRD=") + std::to_string(tcp_id_) + "," + std::to_string(arguments[1].int_value));
        } else if (command == "QIRD" && arguments.size() >= 2) {
                std::lock_guard<std::mutex> lock(mutex_);
                modem_.DecodeHexAppend(rx_buffer_, arguments[3].string_value.c_str(), arguments[3].string_value.size());
        } else if (command == "MIPSTATE" && arguments.size() == 5) {
            if (arguments[0].int_value == tcp_id_) {
                if (arguments[4].string_value == "INITIAL") {
                    connected_ = false;
                } else {
                    connected_ = true;
                }
                xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_INITIALIZED);
            }
        } else if (command == "FIFO_OVERFLOW") {
            xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_ERROR);
            Disconnect();
        }
    });
}

EC800SslTransport::~EC800SslTransport() {
    modem_.UnregisterCommandResponseCallback(command_callback_it_);
}

bool EC800SslTransport::Connect(const char* host, int port) {
    char command[64];

    // Clear bits
    xEventGroupClearBits(event_group_handle_, EC800_SSL_TRANSPORT_CONNECTED | EC800_SSL_TRANSPORT_DISCONNECTED | EC800_SSL_TRANSPORT_ERROR);

    // 检查这个 id 是否已经连接
    sprintf(command, "AT+MIPSTATE=%d", tcp_id_);
    modem_.Command(command);
    auto bits = xEventGroupWaitBits(event_group_handle_, EC800_SSL_TRANSPORT_INITIALIZED, pdTRUE, pdFALSE, pdMS_TO_TICKS(SSL_CONNECT_TIMEOUT_MS));
    if (!(bits & EC800_SSL_TRANSPORT_INITIALIZED)) {
        ESP_LOGE(TAG, "Failed to initialize TCP connection");
        return false;
    }

    // 断开之前的连接
    if (connected_) {
        Disconnect();
    }

    // 场景激活
    sprintf(command, "AT+QIACT=1");
    modem_.Command(command);
    sprintf(command, "AT+QIACT?");
    if (!modem_.Command(command)) {
        ESP_LOGE(TAG, "");
        return false;
    }

    // 设置 SSL 配置
    sprintf(command, "AT+QSSLCFG=\"seclevel\",0,0");
    if (!modem_.Command(command)) {
        ESP_LOGE(TAG, "Failed to set SSL configuration");
        return false;
    }

    // 打开 TCP 连接
    sprintf(command, "AT+QIOPEN=1,%d,\"TCP\",\"%s\",%d,0,0", tcp_id_, host.c_str(), port);
    if (!modem_.Command(command)) {
        ESP_LOGE(TAG, "Failed to open TCP connection");
        return false;
    }

    // 查询连接状态
    sprintf(command, "AT+QISTATE=%d,0", tcp_id_);
    if (!modem_.Command(command)) {
        ESP_LOGE(TAG, "Failed to set HEX encoding");
        return false;
    }

    // 等待连接完成
    bits = xEventGroupWaitBits(event_group_handle_, EC800_SSL_TRANSPORT_CONNECTED | EC800_SSL_TRANSPORT_ERROR, pdTRUE, pdFALSE, SSL_CONNECT_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (bits & EC800_SSL_TRANSPORT_ERROR) {
        ESP_LOGE(TAG, "Failed to connect to %s:%d", host, port);
        return false;
    }
    return true;
}

void EC800SslTransport::Disconnect() {
    if (!connected_) {
        return;
    }
    connected_ = false;
    xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_DISCONNECTED);
    std::string command = "AT+QICLOSE=" + std::to_string(tcp_id_);
    modem_.Command(command);
}

int EC800SslTransport::Send(const char* data, size_t length) {
    const size_t MAX_PACKET_SIZE = 1460 / 2;
    size_t total_sent = 0;

    // 在循环外预先分配command
    std::string command;
    command.reserve(32 + MAX_PACKET_SIZE * 2);  // 预分配最大可能需要的空间

    while (total_sent < length) {
        size_t chunk_size = std::min(length - total_sent, MAX_PACKET_SIZE);

        // 重置command并构建新的命令
        command.clear();
        command = "AT+QISENDEX=" + std::to_string(tcp_id_) + "," ;

        // 直接在command字符串上进行十六进制编码
        modem_.EncodeHexAppend(command, data + total_sent, chunk_size);

        if (!modem_.Command(command)) {
            ESP_LOGE(TAG, "发送数据块失败");
            connected_ = false;
            xEventGroupSetBits(event_group_handle_, EC800_SSL_TRANSPORT_DISCONNECTED);
            return -1;
        }

        command.clear();
        command = "AT+QISEND=0,0";
        modem_.Command(command);

        auto bits = xEventGroupWaitBits(event_group_handle_, EC800_SSL_TRANSPORT_SEND_COMPLETE, pdTRUE, pdFALSE, pdMS_TO_TICKS(SSL_CONNECT_TIMEOUT_MS));
        if (!(bits & EC800_SSL_TRANSPORT_SEND_COMPLETE)) {
            ESP_LOGE(TAG, "未收到发送确认");
            return -1;
        }

        total_sent += chunk_size;
    }
    return length;
}

int EC800SslTransport::Receive(char* buffer, size_t bufferSize) {
    while (rx_buffer_.empty()) {
        auto bits = xEventGroupWaitBits(event_group_handle_, EC800_SSL_TRANSPORT_RECEIVE | EC800_SSL_TRANSPORT_DISCONNECTED, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & EC800_SSL_TRANSPORT_DISCONNECTED) {
            return 0;
        }
        if (!(bits & EC800_SSL_TRANSPORT_RECEIVE)) {
            ESP_LOGE(TAG, "Failed to receive data");
            return -1;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    size_t length = std::min(bufferSize, rx_buffer_.size());
    memcpy(buffer, rx_buffer_.data(), length);
    rx_buffer_.erase(0, length);
    return length;
}
