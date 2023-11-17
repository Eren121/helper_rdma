#include "rdma_client.h"
#include "rdma_server.h"

#include <cstdlib>
#include <string>
#include <climits>
#include <chrono>
#include <iostream>

class Timer {
public:
    Timer(const std::string& title = "") : title_(title), beg_(clock_::now()) {}

    void reset() { beg_ = clock_::now(); }

    void print() {
        if(!title_.empty()) {
            std::cout << title_ << ": ";
        }

        std::cout << "elapsed: " << elapsed() << "s" << std::endl;
    }

    double elapsed() const {
        return std::chrono::duration_cast<second_> (clock_::now() - beg_).count();
    }

private:
    std::string title_;
    typedef std::chrono::high_resolution_clock clock_;
    typedef std::chrono::duration<double, std::ratio<1> > second_;
    std::chrono::time_point<clock_> beg_;
};

int main(int argc, char *argv[])
{
    ENSURE(argc >= 4);

    // Get cmd arguments <-c|-s> <address> <port> <buf_size> <num_trials> of the server
    const std::string addr = argv[2];
    const int port = atoi(argv[3]);

    const uint32_t buf_size = (argc < 5 ? 4'000'000 : static_cast<uint32_t>(atoi(argv[4])));
    const int num_trials = (argc < 6 ? 1'000 : atoi(argv[5]));

    Timer conn_timer;

    if(strcmp(argv[1], "-s") == 0)
    {
        RdmaServer server(buf_size, buf_size, addr, port);
        server.wait_until_connected();

        Timer timer("server");
        for(int i = 0; i < num_trials; i++)
        {
            server.msg_recv([](uint32_t request_sz, uint32_t& response_sz) {

                response_sz = 1;
            });
        }
    }
    else if(strcmp(argv[1], "-c") == 0)
    {
        RdmaClient client(buf_size, buf_size, addr, port);
        client.wait_until_connected();

        Timer timer("client");
        for(int i = 0; i < num_trials; i++)
        {
            client.msg_send(buf_size);
        }
    }
    else
    {
        FATAL_ERROR("Usage: %s (-c|-s) address port", argv[0]);
    }

    const size_t bytes_sent = static_cast<size_t>(num_trials) * static_cast<size_t>(buf_size);
    const double gbits_per_sec = (static_cast<double>(bytes_sent) / conn_timer.elapsed()) / 1e9 * CHAR_BIT;
    printf("Bandwidth: %f Gbit/s\n", gbits_per_sec);


    puts("exit");
    return EXIT_SUCCESS;
}
