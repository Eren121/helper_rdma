#include "rdma_server.h"
#include "rdma_client.h"
#include "spdlog/spdlog.h"

RdmaServer::RdmaServer(uint32_t send_buf_sz, uint32_t recv_buf_sz, const std::string& server_addr, int server_port)
        : RdmaBase(send_buf_sz, recv_buf_sz)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = server_port;

    const int backlog = 10;

    spdlog::info("Created RDMA server to listen on address {}:{}", server_addr, server_port);
    spdlog::info("Created RDMA server buffer sizes: send={}, recv={}", send_buf_sz, recv_buf_sz);

    HENSURE_ERRNO(rdma_bind_addr(m_connection_id, reinterpret_cast<sockaddr*>(&addr)) == 0);
    HENSURE_ERRNO(rdma_listen(m_connection_id, backlog) == 0);
}

RdmaServer::~RdmaServer()
{
}

bool RdmaServer::on_event_received(rdma_cm_event* const event)
{
    switch(event->event)
    {
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            on_conn_request(event->id);
            break;
        
        case RDMA_CM_EVENT_ESTABLISHED:
            on_conn_established(event->id->context);
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

void RdmaServer::on_conn_request(rdma_cm_id* const id)
{
    spdlog::info("Received RDMA connection request");

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

    rdma_conn_param param{};
    HENSURE_ERRNO(rdma_accept(id, &param) == 0);
}

void RdmaServer::on_conn_established(void* user_context)
{
    spdlog::info("RDMA connection established");
}

void RdmaServer::on_disconnect(rdma_cm_id* const id)
{
    spdlog::info("RDMA connection disconnected");
    
    rdma_destroy_qp(id);
    HENSURE_ERRNO(rdma_destroy_id(id) == 0);
}

void RdmaServer::wait_until_connected()
{
    spdlog::info("Waiting incoming RDMA connection...");

    bool stop = false;
    while(!stop)
    {
        const rdma_cm_event event = wait_cm_event();

        switch(event.event)
        {
            case RDMA_CM_EVENT_CONNECT_REQUEST:
                on_conn_request(event.id);
                break;

            case RDMA_CM_EVENT_ESTABLISHED:
                stop = true;
                break;

            default:
                FATAL_ERROR("Unknown RDMA event: %d", static_cast<int>(event.event));
                break;
        }
    }

    spdlog::info("RDMA connection established");
}
