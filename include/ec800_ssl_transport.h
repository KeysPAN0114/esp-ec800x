#ifndef EC800_SSL_TRANSPORT_H
#define EC800_SSL_TRANSPORT_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include "transport.h"
#include "ec800_at_modem.h"

#include <mutex>
#include <string>

#define EC800_SSL_TRANSPORT_CONNECTED BIT0
#define EC800_SSL_TRANSPORT_DISCONNECTED BIT1
#define EC800_SSL_TRANSPORT_ERROR BIT2
#define EC800_SSL_TRANSPORT_RECEIVE BIT3
#define EC800_SSL_TRANSPORT_SEND_COMPLETE BIT4
#define EC800_SSL_TRANSPORT_INITIALIZED BIT5

#define SSL_CONNECT_TIMEOUT_MS 10000

class EC800SslTransport : public Transport {
public:
    EC800SslTransport(EC800AtModem& modem, int tcp_id);
    ~EC800SslTransport();

    bool Connect(const char* host, int port) override;
    void Disconnect() override;
    int Send(const char* data, size_t length) override;
    int Receive(char* buffer, size_t bufferSize) override;

private:
    std::mutex mutex_;
    EC800AtModem& modem_;
    EventGroupHandle_t event_group_handle_;
    int tcp_id_ = 0;
    std::string rx_buffer_;
    std::list<CommandResponseCallback>::iterator command_callback_it_;
};

#endif // EC800_SSL_TRANSPORT_H
