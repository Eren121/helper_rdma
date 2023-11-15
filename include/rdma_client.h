#pragma once

#include "rdma_base.h"
#include <string>

class RdmaClient : public RdmaBase
{
public:
    RdmaClient(uint32_t send_buf_sz, uint32_t recv_buf_sz, const std::string& server_addr, int server_port);
    ~RdmaClient() override;

    void wait_until_connected(Operation first_op) override;

protected:
    bool on_event_received(rdma_cm_event* const event) override;

    void on_addr_resolved(rdma_cm_id* const id);
    void on_route_resolved(rdma_cm_id* const id);
    void on_connect(rdma_cm_id* const id);
    void on_disconnect(rdma_cm_id* const id);
};