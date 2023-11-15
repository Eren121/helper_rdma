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
     */
    virtual void wait_until_connected(Operation first_op) = 0;

    /**
     * Wait until data is sent.
     * Blocking.
     * @param size The size of the data to send.
     * This should be less or equals the sending buffer size.
     * This will send the first `size` bytes of the sending buffer.
     * It is possible to send less data that the sending buffer size to save bandwidth.
     */
    void wait_for_send(uint32_t size);

    /**
     * Wait until data is received.
     * Blocking.
     * @param[out] size The actual size of the data received.
     * This should be less or equals the receiving buffer size.
     */
    void wait_for_recv(uint32_t& size);

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
     * @param size The size of the data to send. If this value is zero, then send the entire buffer.
     */
    void post_send(uint32_t size = 0);


    void disconnect()
    {
        ENSURE_ERRNO(rdma_disconnect(m_connection_id) == 0);
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
    std::vector<uint8_t> m_send_buf;
    std::vector<uint8_t> m_recv_buf;
};