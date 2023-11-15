#pragma once

#include <rdma/rdma_cma.h>
#include <string>

/**
 * Wraps the rdma connection manager (rdma_cm).
 * This is the work that is done before the actual RDMA data transfer.
 * This is just to setup and tear down the connection.
 */
class RdmaCM
{
public:
    RdmaCM();
    ~RdmaCM();

    /// @{
    /** Disallow copies */
    RdmaCM(const RdmaCM&) = delete;
    RdmaCM& operator=(const RdmaCM&) = delete;
    /// @}

    /**
     * Block until the next rdma_cm event is received, and return it.
     */
    rdma_cm_event wait_next_event();

    /**
     * Connect to a rdma_cm server.
     * Then, this instance becomes a rdma_cm client.
     * @param addr The TCP address to connect.
     * @param port The TCP port to connect.
     * @note This is blocking, and returns when the connection is established.
     */
    void connect_to_server(const std::string& addr, int port);

    /**
     * Start a rdma_cm server on the specified address and port.
     * Then, this instance becomes a rdma_cm server.
     * @param port The port to listen to TCP request.
     * @note This is blocking, and returns when the connection with one client is established.
     */
    void run_server(int port);

private:
    /**
     * Asynchronous events are reported to users through event channels.
     * RAII.
     */
    rdma_event_channel* m_channel{nullptr};

    /**
     * This is rhoughly analogous to a socket.
     * RAII.
     */
    rdma_cm_id* m_id{nullptr};
};