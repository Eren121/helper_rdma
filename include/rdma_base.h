#pragma once

#include "helper_errno.h"
#include <rdma/rdma_cma.h>
#include <netdb.h>
#include <pthread.h>

#include <cstdlib>
#include <thread>
#include <functional>
#include <vector>

/**
 * Common base class for both the RDMA client and server.
 */
class RdmaBase
{
public:
    /**
     * Represents a block of contiguous memory.
     * This has a pointer to a data and a size.
     */
    struct Buffer
    {
        uint8_t* data{nullptr};
        uint32_t size{0};
    };

    /**
     * @param send_buf_sz Size of the send buffer.
     * How many bytes should be allocated in the pinned memory region to handle "send" operations.
     * @param recv_buf_sz Size of the receiving buffer.
     * How many bytes should be allocated in the pinned memory region to handle "receive" operations.
     */
    RdmaBase(uint32_t send_buf_sz, uint32_t recv_buf_sz);

    /**
     * Destroys all RDMA resources.
     */
    virtual ~RdmaBase();

    /// {@
    /**
     * Non-copiable.
     */
    RdmaBase(const RdmaBase&) = delete;
    RdmaBase& operator=(const RdmaBase&) = delete;
    /// @}

    /**
     * Get the sending buffer data.
     * @note Does not return the underlying `std::vector` to prevent to allow to modify its size.
     */
    Buffer get_send_buf();

    /**
     * Get the receiving buffer data.
     * @note Does not return the underlying `std::vector` to prevent to allow to modify its size.
     */
    Buffer get_recv_buf();

    /**
     * Wait the next RDMA connection manager event.
     * Wait only one event.
     * Blocking until an event occurs.
     * @returns The event that occurred.
     * @note Automatically acknowledge the event before returning it.
     */
    rdma_cm_event wait_cm_event();

    /**
     * Wait the next completion queue event.
     * Wait only one event.
     * Blocking until an event occurs.
     * @returns The event that occured.
     */
    ibv_wc wait_event();

    /**
     * Wait until the RDMA connection is setup, and the RDMA operations are ready to start.
     * Blocking.
     * @param first_op Which operation should be done first, either a receive or a send.
     * Because we need to add elements to completion queue before the work is arriving.
     *
     * @param sender false if this function should pre-post a receiving request.
     *               true otherwise.
     * @note This will pre-post a recv request to be sure the queue is always ready to receive a message.
     */
    virtual void wait_until_connected() = 0;

    /**
     * Wait until data is sent.
     * Does not post any "send" request, this should have been post beforehand.
     * Blocking.
     */
    void wait_for_send();

    /**
     * Wait until 1 send and 1 receive event are polled (in any order).
     * Blocking.
     * @param[out] size The actual size of the data received.
     * This should be less or equals the receiving buffer size.
     */
    void wait_for_1send_1recv(uint32_t& size);

    /**
     * Wait until data is received.
     * Does not post any "recv" request, this should have been post beforehand.
     * Blocking.
     * @param[out] size The actual size of the data received.
     * This should be less or equals the receiving buffer size.
     */
    void wait_for_recv(uint32_t& size);

    /**
     * Send a message. Each message has a response.
     * The sending buffer should contains the message to send.
     * After this function, the receiving buffers stores the response.
     * @param[in] request_sz The size of the data to send from the sending buffer.
     * @return The response.
     * The size of the response is less or equals the receiving buffer size,
     * and the data pointer is the same as the receiving buffer.
     */
    Buffer msg_send(uint32_t request_sz)
    {
        post_receive();
        post_send(request_sz);

        Buffer response;
        response.data = m_recv_buf.data();
        wait_for_1send_1recv(response.size);

        return response;
    }

    /**
     * Receive counterpart of sending a message.
     * Each `msg_send` should match a `msg_recv`.
     * @param handler The method to execute to process the request.
     * Should be of signature `void(uint32_t request_sz, uint32_t& response_sz)`
     * It should fill the sending buffer with a response and compute the response size.
     */
    template<typename Handler>
    void msg_recv(Handler handler)
    {
        post_receive();

        uint32_t request_sz;
        wait_for_recv(request_sz);

        uint32_t response_sz;
        handler(request_sz, response_sz);

        post_send(response_sz);
        wait_for_send();
    }

    void run_event_loop();

    using Callback = std::function<void()>;

    /**
     * Called when the connection is established.
     * The queue pair is ready in the callback.
     * 
     * The callback should be called by the child class.
     */
    void set_connection_established_callback(Callback callback)
    {
        m_cb_connection_ready = callback;
    }

    /**
     * Post a receive work request (WR)
     */
    void post_receive();

    /**
     * Post a send work request (WR).
     * @param size The size of the data to send.
     */
    void post_send(uint32_t size);


    void disconnect()
    {
        HENSURE_ERRNO(rdma_disconnect(m_connection_id) == 0);
    }

    Callback on_recv_complete;
    Callback on_send_complete;

protected:
    static void build_qp_init_attr(ibv_cq* const cq, ibv_qp_init_attr* out);

    // returns false to stop the RDMA connection, or true to continue the polling loop.
    virtual bool on_event_received(rdma_cm_event* const event) = 0;

    virtual void poll_handler();

    // Setup the context (if not already exists) from the ibv_context
    void setup_context(ibv_context* const context);

    // For the connection manager
    rdma_event_channel* m_event_channel = nullptr;

    // The QP used to exchange data
    // This should be set by the child class
    ibv_qp* m_qp = nullptr;

    // The ID associated to the QP
    // This should be set by the child class
    rdma_cm_id* m_qp_id = nullptr;

    // This is the ID for the connection itself
    // Created/Destroyed by RdmaBase
    rdma_cm_id* m_connection_id = nullptr;


    // For the context
    ibv_context* m_context = nullptr;
    ibv_pd* m_pd = nullptr;
    ibv_cq* m_cq = nullptr;
    ibv_mr* m_send_mr = nullptr;
    ibv_mr* m_recv_mr = nullptr;
    ibv_comp_channel* m_comp_channel = nullptr;

    pthread_t m_handler_thread;

    Callback m_cb_connection_ready;
    Callback m_cb_qp_ready;

private:
    bool m_event_alive{false};
    ibv_cq* m_event_cq{nullptr};
    void* m_event_user_context{nullptr};

    std::vector<uint8_t> m_send_buf;
    std::vector<uint8_t> m_recv_buf;
};