#include "rdma_cm.h"
#include "helper_tcp/helper_errno.h"

RdmaCM::RdmaCM()
{
    m_channel = rdma_create_event_channel();
    ENSURE_ERRNO(m_channel != nullptr);
    ENSURE_ERRNO(rdma_create_id(m_channel, &m_id, nullptr, RDMA_PS_TCP) == 0);
}

RdmaCM::~RdmaCM()
{
    if(m_channel)
    {
        rdma_destroy_event_channel(m_channel);
        m_channel = nullptr;
    }

    if(m_id)
    {
        rdma_destroy_id(m_id);
        m_id = nullptr;
    }
}

rdma_cm_event RdmaCM::wait_next_event()
{
    rdma_cm_event* event = nullptr;

    // Block until the next event is received
    ENSURE_ERRNO(rdma_get_cm_event(m_event_channel, &event) != 0)

    const rdma_cm_event event_copy = *event;

    // Each event should be aknowledged
    // This also frees the event
    ENSURE_ERRNO(rdma_ack_cm_event(event) == 0);

    return event_copy;
}

void RdmaCM::connect_to_server(const std::string& addr, int port)
{
    {
        const int timeout_ms = 60'000; // 1 minute timeout

        sockaddr_in sock_addr;
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_port = port;
        ENSURE_ERRNO(inet_aton(addr.c_str(), reinterpret_cast<in_addr*>(&sock_addr.sin_addr.s_addr)) >= 0);
        ENSURE_ERRNO(rdma_resolve_addr(m_connection_id, nullptr, reinterpret_cast<sockaddr*>(&sock_addr), timeout_ms) == 0);
    }

    bool done = false;

    while(!done)
    {
        const rdma_cm_event event = wait_next_event();

        switch(event.event)
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
                fatal_error("Unknown RDMA event: %d", (int)event->event);
                break;
        }
    }
}

void RdmaCM::run_server(int port)
{
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = server_port;
        ENSURE_ERRNO(inet_aton(server_addr.c_str(), reinterpret_cast<in_addr*>(&addr.sin_addr.s_addr)) >= 0);

        ENSURE_ERRNO(rdma_bind_addr(m_connection_id, reinterpret_cast<sockaddr*>(&addr)) == 0);

        // https://general.openfabrics.narkive.com/OnmkvJFb/ofa-rdma-listen-backlog
        // > The backlog dictates how many pending rdma connect requests the kernel
        // > will keep in-queue for your application to accept/reject.
        // We have only one client, so one
        const int backlog = 1;
        ENSURE_ERRNO(rdma_listen(m_connection_id, backlog) == 0);
    }

    bool done = false;

    while(!done)
    {
        const rdma_cm_event event = wait_next_event();

        switch(event.event)
        {
            case RDMA_CM_EVENT_CONNECT_REQUEST:
            {
                rdma_conn_param param;
                memset_zero(&param);
                ENSURE_ERRNO(rdma_accept(event->id, &param) == 0);
            }
                break;

            case RDMA_CM_EVENT_ESTABLISHED:
                done = true;
                break;

            case RDMA_CM_EVENT_DISCONNECTED:
                // Disconnected immediately
                done = true;
                break;

            default:
                fatal_error("Unknown RDMA event: %d", (int)event->event);
                done = true;
                break;
        }
    }
}