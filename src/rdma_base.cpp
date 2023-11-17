#include "rdma_base.h"
#include <cassert>

RdmaBase::RdmaBase(uint32_t send_buf_sz, uint32_t recv_buf_sz)
    : m_send_buf(send_buf_sz),
      m_recv_buf(recv_buf_sz)
{
    // Create RDMA communication manager event channel
    m_event_channel = rdma_create_event_channel();
    ENSURE_ERRNO(m_event_channel != nullptr);

    // Create RDMA communication manager ID
    // RDMA_PS_TCP == RC QP (Reliable Connection Queue Pair, like TCP)
    ENSURE_ERRNO(rdma_create_id(m_event_channel, &m_connection_id, nullptr, RDMA_PS_TCP) == 0);
}

RdmaBase::~RdmaBase()
{
    // Destroy RDMA resources
    // Like RAII

    if(m_connection_id)
    {
        ENSURE_ERRNO(rdma_destroy_id(m_connection_id) == 0);
        m_connection_id = nullptr;
    }

    if(m_event_channel)
    {
        rdma_destroy_event_channel(m_event_channel);
        m_event_channel = nullptr;
    }

    if(m_send_mr)
    {
        ENSURE_ERRNO(ibv_dereg_mr(m_send_mr) == 0);
        m_send_mr = nullptr;
    }

    if(m_recv_mr)
    {
        ENSURE_ERRNO(ibv_dereg_mr(m_recv_mr) == 0);
        m_recv_mr = nullptr;
    }
}

RdmaBase::Buffer RdmaBase::get_send_buf()
{
    return {
        .data = m_send_buf.data(),
        .size = static_cast<uint32_t>(m_send_buf.size())
    };
}

RdmaBase::Buffer RdmaBase::get_recv_buf()
{
    return {
        .data = m_recv_buf.data(),
        .size = static_cast<uint32_t>(m_send_buf.size())
    };
}

void RdmaBase::wait_for_send()
{
    const ibv_wc wc = wait_event();

    if(wc.opcode != IBV_WC_SEND)
    {
        FATAL_ERROR("Expected IBV_WC_SEND event, got something different.");
    }
}

void RdmaBase::wait_for_1send_1recv(uint32_t& size)
{
    int send_count = 0;
    int recv_count = 0;

    for(int i = 0; i < 2; i++)
    {
        const ibv_wc wc = wait_event();

        if(wc.opcode == IBV_WC_SEND)
        {
            send_count++;
        }
        else if(wc.opcode & IBV_WC_RECV)
        {
            recv_count++;
            size = wc.byte_len;
        }
        else
        {
            FATAL_ERROR("Expected IBV_WC_SEND or IBV_WC_RECV event, got something different.");
        }
    }

    if(send_count != 1 || recv_count != 1)
    {
        FATAL_ERROR("Expected exactly 1 send and 1 recv, got %d sends and %d receives", send_count, recv_count);
    }
}


void RdmaBase::wait_for_recv(uint32_t& size)
{
    const ibv_wc wc = wait_event();

    if(!(wc.opcode & IBV_WC_RECV))
    {
        FATAL_ERROR("Next event should be IBV_WC_RECV");
    }

    // `ibv_wc.byte_len` stores the actual data received
    size = wc.byte_len;
}

rdma_cm_event RdmaBase::wait_cm_event()
{
    rdma_cm_event* event = nullptr;
    ENSURE_ERRNO(rdma_get_cm_event(m_event_channel, &event) == 0);

    // The event needs to be copied because acknowledging the event frees it
    rdma_cm_event copy = *event;

    ENSURE_ERRNO(rdma_ack_cm_event(event) == 0);

    return copy;
}

ibv_wc RdmaBase::wait_event()
{
    ibv_cq* cq{nullptr};
    void* user_context{nullptr};
    ibv_wc ret{};

    ENSURE_ERRNO(ibv_get_cq_event(m_comp_channel, &cq, &user_context) == 0);
    ENSURE_ERRNO(ibv_req_notify_cq(cq, 0) == 0);

    ibv_ack_cq_events(cq, 1); // Each event should be acknowledged

    // Wait first event
    {
        const int num_completions = ibv_poll_cq(cq, 1, &ret);
        ENSURE_ERRNO(num_completions == 1);

        if(ret.status != IBV_WC_SUCCESS)
        {
            FATAL_ERROR("Failed status %s (%d) for wr_id %d\n",
                        ibv_wc_status_str(ret.status),
                        ret.status,
                        (int) ret.wr_id);
        }
    }

    // Second event should be empty to notify end of queue
    {
        ibv_wc tmp{};
        const int num_completions = ibv_poll_cq(cq, 1, &tmp);
        ENSURE_ERRNO(num_completions == 0);
    }

    return ret;
}

void RdmaBase::run_event_loop()
{
    printf("start listen to rdma_cm events\n");

    while(true)
    {
        rdma_cm_event* event = nullptr;

        if(rdma_get_cm_event(m_event_channel, &event) != 0)
        {
            break;
        }

        rdma_cm_event event_copy = *event;
        // Each event should be acknowledged
        // This also free the event
        ENSURE_ERRNO(rdma_ack_cm_event(event) == 0);

        printf("received rdcma_cm event id=%d\n", (int) event_copy.event);

        if(!on_event_received(&event_copy))
        {
            break;
        }
    }

    printf("stop listen to rdma_cm events\n");
}

void RdmaBase::setup_context(ibv_context* const context)
{
    // We can't handle more than one context
    if(m_context)
    {
        if(m_context != context)
        {
            FATAL_ERROR("Can't handle more than one context");
        }

        return;
    }

    m_context = context;

    m_pd = ibv_alloc_pd(context);
    ENSURE_ERRNO(m_pd != nullptr);

    m_comp_channel = ibv_create_comp_channel(context);
    ENSURE_ERRNO(m_comp_channel != nullptr);

    const int cq_size = 1'000;
    m_cq = ibv_create_cq(context, cq_size, nullptr, m_comp_channel, 0);
    ENSURE_ERRNO(m_cq != nullptr);

    ENSURE_ERRNO(ibv_req_notify_cq(m_cq, 0) == 0);

    /*
    // Function pointer returning void* and taking one void* parameter
    void* (* handler_pthread)(void*) = [](void* user_arg) -> void* {
        RdmaBase* const self = static_cast<RdmaBase*>(user_arg);
        self->poll_handler();
        return nullptr;
    };

    // Launch thread
    ENSURE_ERRNO(pthread_create(&m_handler_thread, nullptr, handler_pthread, this) == 0);
    */

    // Register memory region
    const int access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;

    m_send_mr = ibv_reg_mr(m_pd, m_send_buf.data(), m_send_buf.size(), access);
    ENSURE_ERRNO(m_send_mr != nullptr);

    m_recv_mr = ibv_reg_mr(m_pd, m_recv_buf.data(), m_recv_buf.size(), access);
    ENSURE_ERRNO(m_recv_mr != nullptr);
}

void RdmaBase::poll_handler()
{
    ibv_cq* cq;
    void* user_context = nullptr;

    while(1)
    {
        // The thread will never stops
        // It will be blocking inside `ibv_get_cq_event()` forever when the connection stops
        // But it just works anyway, when `main()` exits, this thread will be killed

        ibv_wc wc{};

        ENSURE_ERRNO(ibv_get_cq_event(m_comp_channel, &cq, &user_context) == 0);
        ENSURE_ERRNO(ibv_req_notify_cq(cq, 0) == 0);

        ibv_ack_cq_events(cq, 1); // Each event should be acknowledged

        while(1)
        {
            const int num_completions = ibv_poll_cq(cq, 1, &wc);
            ENSURE_ERRNO(num_completions >= 0);

            if(num_completions == 0)
            {
                break;
            }

            if(wc.status != IBV_WC_SUCCESS)
            {
                FATAL_ERROR("Failed status %s (%d) for wr_id %d\n",
                            ibv_wc_status_str(wc.status),
                            wc.status,
                            (int) wc.wr_id);
            }

            if(wc.opcode & IBV_WC_RECV)
            {
                //printf("received message: %zu\n", m_buf.size() * sizeof(m_buf[0]));
                //printf("Received: %s\n", m_buf.data());

                if(on_recv_complete)
                {
                    on_recv_complete();
                }
            } else if(wc.opcode == IBV_WC_SEND)
            {
                printf("send completed successfully.\n");

                if(on_send_complete)
                {
                    on_send_complete();
                }
            }
        }
    }

    puts("exit polling thread");
}

void RdmaBase::post_receive()
{
    ibv_recv_wr wr;
    ibv_recv_wr* bad_wr = nullptr;
    ibv_sge sge;

    // Only 1 scatter/gather entry (SGE)

    wr.wr_id = 123; // Arbitrary
    wr.next = nullptr;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uintptr_t) m_recv_buf.data();
    sge.length = m_recv_buf.size();
    sge.lkey = m_recv_mr->lkey;

    assert(m_qp != nullptr);
    ENSURE_ERRNO(ibv_post_recv(m_qp, &wr, &bad_wr) == 0);
}

void RdmaBase::post_send(uint32_t size)
{
    ibv_send_wr wr{};
    ibv_send_wr* bad_wr = nullptr;
    ibv_sge sge{};

    // Only 1 scatter/gather entry (SGE)

    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr_id = 123; // Arbitrary
    wr.next = nullptr;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    //printf("%p/%p\n", m_buf.data(), m_mr->addr);

    sge.addr = (uintptr_t) m_send_buf.data();
    sge.length = (size == 0 ? m_send_buf.size() : size);
    sge.lkey = m_send_mr->lkey;

    assert(m_qp != nullptr);
    ENSURE_ERRNO(ibv_post_send(m_qp, &wr, &bad_wr) == 0);
}

void RdmaBase::build_qp_init_attr(ibv_cq* const cq, ibv_qp_init_attr* qp_attr)
{
    // Initialize to zero
    std::memset(qp_attr, 0, sizeof(*qp_attr));

    qp_attr->send_cq = cq;
    qp_attr->recv_cq = cq;
    qp_attr->qp_type = IBV_QPT_RC;

    qp_attr->cap.max_send_wr = 1'000;
    qp_attr->cap.max_recv_wr = 1'000;
    qp_attr->cap.max_send_sge = 1;
    qp_attr->cap.max_recv_sge = 1;
}