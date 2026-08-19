#include "load_balancer.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>

// Entry point of the load balancer application
int main(int argc, char* argv[]) {

    // Stores the comma-separated backend URLs
    std::string backends_str;

    // Default port on which the load balancer will listen
    int port = 3030;


    // ---------------------------------------------------------
    // 1. Parse command-line arguments
    // ---------------------------------------------------------

    for (int i = 1; i < argc; ++i) {

        std::string arg = argv[i];

        // Read backend server URLs
        if ((arg == "--backends" || arg == "-backends") && i + 1 < argc) {
            backends_str = argv[++i];

        // Read load balancer listening port
        } else if ((arg == "--port" || arg == "-port") && i + 1 < argc) {

            try {
                // Convert port from string to integer
                port = std::stoi(argv[++i]);

            } catch (...) {

                // If conversion fails, use the default port
                std::cerr << "Invalid port number. Using default 3030.\n";
            }
        }
    }


    // ---------------------------------------------------------
    // 2. Validate backend configuration
    // ---------------------------------------------------------

    // Load balancer cannot work without backend servers
    if (backends_str.empty()) {

        std::cerr << "Error: Please provide one or more backends to load balance.\n"
                  << "Usage: simplelb --backends <url1,url2,...> [--port <port>]\n"
                  << "Example: simplelb --backends "
                     "http://127.0.0.1:3031,http://127.0.0.1:3032 "
                     "--port 3030\n";

        return 1;
    }


    // ---------------------------------------------------------
    // 3. Initialize socket library
    // ---------------------------------------------------------

    // Required on Windows for Winsock.
    // On Linux this function does nothing.
    init_sockets();


    // ---------------------------------------------------------
    // 4. Create the backend server pool
    // ---------------------------------------------------------

    // ServerPool stores all backend servers and
    // selects a backend for incoming requests.
    ServerPool pool;


    // Use stringstream to split backend URLs by comma
    std::stringstream ss(backends_str);
    std::string token;

    std::cout << "Configured backends:\n";


    // ---------------------------------------------------------
    // 5. Parse and add each backend server
    // ---------------------------------------------------------

    while (std::getline(ss, token, ',')) {

        // Ignore empty backend entries
        if (token.empty())
            continue;


        // Create a Backend object
        auto backend = std::make_shared<Backend>();

        // Keep the original URL for logging and identification
        backend->original_url = token;


        // Extract hostname and port from the backend URL
        if (parse_backend_url(
                token,
                backend->host,
                backend->port)) {

            // Add the valid backend to the pool
            pool.add_backend(backend);

            std::cout << "  - "
                      << backend->host << ":"
                      << backend->port
                      << " (parsed from " << token << ")\n";

        } else {

            // Ignore invalid backend URLs
            std::cerr << "  - Failed to parse backend URL: "
                      << token << "\n";
        }
    }


    // ---------------------------------------------------------
    // 6. Make sure at least one backend is available
    // ---------------------------------------------------------

    if (pool.get_size() == 0) {

        std::cerr << "Error: No valid backends parsed. Exiting.\n";

        // Release socket resources
        cleanup_sockets();

        return 1;
    }


    // ---------------------------------------------------------
    // 7. Start background health-check thread
    // ---------------------------------------------------------

    // This thread periodically checks whether
    // each backend server is alive or down.
    spawn_detached_thread(
        health_check_thread_runner,
        &pool
    );


    // ---------------------------------------------------------
    // 8. Create the load balancer TCP socket
    // ---------------------------------------------------------

    // AF_INET     -> IPv4
    // SOCK_STREAM -> TCP socket
    // IPPROTO_TCP -> TCP protocol
    socket_t server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );


    // Check whether socket creation succeeded
    if (server_fd == INVALID_SOCKET_VALUE) {

        std::cerr << "Error: Failed to create socket.\n";

        cleanup_sockets();

        return 1;
    }


    // ---------------------------------------------------------
    // 9. Configure socket options
    // ---------------------------------------------------------

    // Allows the server to reuse the port after restarting.
    int opt = 1;

    setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char*)&opt,
        sizeof(opt)
    );


    // ---------------------------------------------------------
    // 10. Configure server address
    // ---------------------------------------------------------

    struct sockaddr_in address;

    // Clear the address structure
    std::memset(
        &address,
        0,
        sizeof(address)
    );

    // Use IPv4
    address.sin_family = AF_INET;

    // Listen on all available network interfaces
    address.sin_addr.s_addr = INADDR_ANY;

    // Convert port to network byte order
    address.sin_port = htons(port);


    // ---------------------------------------------------------
    // 11. Bind socket to IP address and port
    // ---------------------------------------------------------

    if (bind(
            server_fd,
            (struct sockaddr*)&address,
            sizeof(address)
        ) < 0) {

        std::cerr << "Error: Bind failed on port "
                  << port << ".\n";

        close_socket(server_fd);
        cleanup_sockets();

        return 1;
    }


    // ---------------------------------------------------------
    // 12. Start listening for client connections
    // ---------------------------------------------------------

    if (listen(
            server_fd,
            SOMAXCONN
        ) < 0) {

        std::cerr << "Error: Listen failed.\n";

        close_socket(server_fd);
        cleanup_sockets();

        return 1;
    }


    std::cout
        << "\nC++ Load Balancer started at http://127.0.0.1:"
        << port << "\n";


    // ---------------------------------------------------------
    // 13. Continuously accept client connections
    // ---------------------------------------------------------

    while (true) {

        struct sockaddr_in client_addr;

        // Stores the size of the client address structure
        socklen_t addrlen = sizeof(client_addr);


        // Wait for a new client connection.
        // accept() returns a separate socket for that client.
        socket_t client_fd = accept(
            server_fd,
            (struct sockaddr*)&client_addr,
            &addrlen
        );


        // If accept fails, continue waiting for clients
        if (client_fd == INVALID_SOCKET_VALUE) {
            continue;
        }


        // -----------------------------------------------------
        // 14. Prepare arguments for the client thread
        // -----------------------------------------------------

        // Pass both:
        // 1. Client socket
        // 2. Shared backend pool
        ClientThreadArg* arg =
            new ClientThreadArg{
                client_fd,
                &pool
            };


        // -----------------------------------------------------
        // 15. Handle this client in a separate thread
        // -----------------------------------------------------

        // Each client gets its own thread so that
        // multiple clients can be handled concurrently.
        spawn_detached_thread(
            client_thread_runner,
            arg
        );
    }


    // This section is normally unreachable because
    // the server runs continuously inside while(true).
    close_socket(server_fd);
    cleanup_sockets();

    return 0;
}
