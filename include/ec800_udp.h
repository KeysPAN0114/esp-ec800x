#ifndef EC800_UDP_H
#define EC800_UDP_H

#include "udp.h"
#include "ec800_at_modem.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#define EC800_UDP_CONNECTED BIT0
#define EC800_UDP_DISCONNECTED BIT1
#define EC800_UDP_ERROR BIT2
#define EC800_UDP_RECEIVE BIT3
#define EC800_UDP_SEND_COMPLETE BIT4
#define EC800_UDP_INITIALIZED BIT5

#define UDP_CONNECT_TIMEOUT_MS 10000

class EC800Udp : public Udp {
public:
    EC800Udp(EC800AtModem& modem, int udp_id);
    ~EC800Udp();

    bool Connect(const std::string& host, int port) override;
    void Disconnect() override;
    int Send(const std::string& data) override;

private:
    EC800AtModem& modem_;
    int udp_id_;
    EventGroupHandle_t event_group_handle_;
    std::list<EcCommandResponseCallback>::iterator command_callback_it_;
};

#endif // EC800_UDP_H
