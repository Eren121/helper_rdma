#include "rdma_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const int timeout_ms = 1'000 * 60; // 1min

RdmaClient::RdmaClient(uint32_t send_buf_sz, uint32_t recv_buf_sz, const std::string& server_addr, int server_port)
    : RdmaBase(send_buf_sz, recv_buf_sz)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = server_port;

    HENSURE_ERRNO(inet_aton(server_addr.c_str(), reinterpret_cast<in_addr*>(&addr.sin_addr.s_addr)) >= 0);
    HENSURE_ERRNO(rdma_resolve_addr(m_connection_id, nullptr, reinterpret_cast<sockaddr*>(&addr), timeout_ms) == 0);
}

RdmaClient::~RdmaClient()
{
    // PRevent RdmaBase to destroy it
    // The client should not destroy it
    m_connection_id = nullptr;
}

bool RdmaClient::on_event_received(rdma_cm_event* const event)
{
    switch(event->event)
    {
        case RDMA_CM_EVENT_ADDR_RESOLVED:
            on_addr_resolved(event->id);
            break;

        case RDMA_CM_EVENT_ROUTE_RESOLVED:
            on_route_resolved(event->id);
            break;

        case RDMA_CM_EVENT_ESTABLISHED:
            on_connect(event->id);
            break;

        case RDMA_CM_EVENT_DISCONNECTED:
            on_disconnect(event->id);
            return false; // Breaks the event loop

        default:
            FATAL_ERROR("on_event_received(): Unknown RDMA event: %d", (int)event->event);
            break;
    }

    return true;
}

void RdmaClient::on_addr_resolved(rdma_cm_id* const id)
{
    setup_context(id->verbs);

    ibv_qp_init_attr attr{};
    build_qp_init_attr(m_cq, &attr);
    HENSURE_ERRNO(rdma_create_qp(id, m_pd, &attr) == 0);

    // The ID that will be use for send/recv
    m_qp = id->qp;
    m_qp_id = id;

    // Pre-post receive event on the to be sure there is one receive work
    // before the remote sends a message
    post_receive();

    const int timeout_ms = 1'000 * 60; // 1min
    HENSURE_ERRNO(rdma_resolve_route(id, timeout_ms) == 0);
}

void RdmaClient::on_route_resolved(rdma_cm_id* const id)
{
    rdma_conn_param param{};
    HENSURE_ERRNO(rdma_connect(id, &param) == 0);
}

void RdmaClient::on_connect(rdma_cm_id* const id)
{
    puts("on_connect");

    // Call callback
    if(m_cb_connection_ready)
    {
        m_cb_connection_ready();
    }
}

void RdmaClient::on_disconnect(rdma_cm_id* const id)
{
    puts("on_disconnect");

    rdma_destroy_qp(id);
    HENSURE_ERRNO(rdma_destroy_id(id) == 0);
}

void RdmaClient::wait_until_connected()
{
    bool stop = false;
    while(!stop)
    {
        const rdma_cm_event event = wait_cm_event();

        switch(event.event)
        {
            case RDMA_CM_EVENT_ADDR_RESOLVED:
                on_addr_resolved(event.id);
                break;

            case RDMA_CM_EVENT_ROUTE_RESOLVED:
                on_route_resolved(event.id);
                break;

            case RDMA_CM_EVENT_ESTABLISHED:
                stop = true;
                break;

            default:
                FATAL_ERROR("Unknown RDMA event: %d", static_cast<int>(event.event));
                break;
        }
    }
}
