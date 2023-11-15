#pragma once

#include "rdma_base.h"

class RdmaServer : public RdmaBase
{
public:
    RdmaServer(uint32_t send_buf_sz, uint32_t recv_buf_sz, const std::string& server_addr, int server_port);
    ~RdmaServer() override;

    void wait_until_connected() override;

protected:
    bool on_event_received(rdma_cm_event* const event) override;
    
    void on_conn_request(rdma_cm_id* const id);
    void on_conn_established(void* user_context);
    void on_disconnect(rdma_cm_id* const id);
};
