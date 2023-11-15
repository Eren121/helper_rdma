#include "rdma_server.h"
#include <cassert>

RdmaServer::RdmaServer(uint32_t send_buf_sz, uint32_t recv_buf_sz, const std::string& server_addr, int server_port)
        : RdmaBase(send_buf_sz, recv_buf_sz)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = server_port;

    const int backlog = 10;
    
    ENSURE_ERRNO(rdma_bind_addr(m_connection_id, reinterpret_cast<sockaddr*>(&addr)) == 0);
    ENSURE_ERRNO(rdma_listen(m_connection_id, backlog) == 0);
}

RdmaServer::~RdmaServer()
{
    ENSURE(pthread_cancel(m_handler_thread) == 0);
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
            FATAL_ERROR("on_event_received(): Unknown RDMA event: %d", (int)event->event);
            break;
    }

    return true;
}

void RdmaServer::on_conn_request(rdma_cm_id* const id)
{
    setup_context(id->verbs);
    
    ibv_qp_init_attr attr;
    build_qp_init_attr(m_cq, &attr);
    ENSURE_ERRNO(rdma_create_qp(id, m_pd, &attr) == 0);
    
    // The ID that will be use for send/recv
    m_qp = id->qp;
    m_qp_id = id;

    // Call callback
    if(m_cb_qp_ready)
    {
        m_cb_qp_ready();
    }

    rdma_conn_param param{};
    ENSURE_ERRNO(rdma_accept(id, &param) == 0);
}

void RdmaServer::on_conn_established(void* user_context)
{
}

void RdmaServer::on_disconnect(rdma_cm_id* const id)
{
    puts("on_disconnect");
    
    rdma_destroy_qp(id);
    ENSURE_ERRNO(rdma_destroy_id(id) == 0);
}

void RdmaServer::wait_until_connected(Operation first_op)
{
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
}
