#include "load_balancer.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>

// Main function of the load balancer
int main(int argc, char* argv[]) {

    std::string backends_str;
    int port = 3030; // Default load balancer port

    // Command line argument parsing
    for (int i = 1; i < argc; ++i) {

        std::string arg = argv[i];

        // Get the backend URLs
        if ((arg == "--backends" || arg == "-backends") && i + 1 < argc) {
            backends_str = argv[++i];

        // Get the load balancer port
        } else if ((arg == "--port" || arg == "-port") && i + 1 < argc) {

            try {
                port = std::stoi(argv[++i]);
            } catch (...) {
                // If the port is invalid, use the default port
                std::cerr << "Invalid port number. Using default 3030.\n";
            }
        }
    }

    // Check whether backend URLs were provided
    if (backends_str.empty()) {

        std::cerr << "Error: Please provide one or more backends to load balance.\n"
                  << "Usage: simplelb --backends <url1,url2,...> [--port <port>]\n"
                  << "Example: simplelb --backends http://127.0.0.1:3031,http://127.0.0.1:3032 --port 3030\n";

        return 1;
    }

    // Initialize platform sockets
    // Required for Windows, does nothing on Linux
    init_sockets();


    // Create the pool which stores all backend servers
    ServerPool pool;

    // Stringstream is used to split the backend URLs using comma
    std::stringstream ss(backends_str);
    std::string token;

    std::cout << "Configured backends:\n";


    // Read each backend URL one by one
    while (std::getline(ss, token, ',')) {

        if (token.empty())
            continue;

        // Create a new backend object
        auto backend = std::make_shared<Backend>();

        // Store the original backend URL
        backend->original_url = token;

        // Parse the backend URL and extract host and port
        if (parse_backend_url(
                token,
                backend->host,
                backend->port)) {

            // Add the valid backend to the server pool
            pool.add_backend(backend);

            std::cout << "  - "
                      << backend->host << ":"
                      << backend->port
                      << " (parsed from " << token << ")\n";

        } else {

            // If the URL cannot be parsed, do not add it
            std::cerr << "  - Failed to parse backend URL: "
                      << token << "\n";
        }
    }


    // Check whether at least one valid backend exists
    if (pool.get_size() == 0) {

        std::cerr << "Error: No valid backends parsed. Exiting.\n";

        // Clean up the socket library
        cleanup_sockets();

        return 1;
    }


    // Start background health checking thread
    // This continuously checks whether the backend servers are alive
    spawn_detached_thread(
        health_check_thread_runner,
        &pool
    );


    // Create the listening socket
    // AF_INET    -> IPv4
    // SOCK_STREAM -> TCP
    // IPPROTO_TCP -> TCP protocol
    socket_t server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );


    // Check whether socket creation was successful
    if (server_fd == INVALID_SOCKET_VALUE) {

        std::cerr << "Error: Failed to create socket.\n";

        cleanup_sockets();

        return 1;
    }


    // Allow the socket address and port to be reused
    // This is useful when the server is restarted
    int opt = 1;

    setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char*)&opt,
        sizeof(opt)
    );


    // Store the address information of the load balancer
    struct sockaddr_in address;

    // Clear the address structure
    std::memset(
        &address,
        0,
        sizeof(address)
    );

    // Use IPv4
    address.sin_family = AF_INET;

    // INADDR_ANY means listen on all network interfaces
    address.sin_addr.s_addr = INADDR_ANY;

    // Set the port number
    // htons() converts the port into network byte order
    address.sin_port = htons(port);


    // Bind the socket to the IP address and port
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


    // Put the socket into listening mode
    // The load balancer can now accept incoming client connections
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


    // Continuously accept client connections
    while (true) {

        struct sockaddr_in client_addr;

        // Stores the size of the client address structure
        socklen_t addrlen = sizeof(client_addr);


        // Accept a new client connection
        // accept() returns a new socket for that client
        socket_t client_fd = accept(
            server_fd,
            (struct sockaddr*)&client_addr,
            &addrlen
        );


        // If accepting the connection fails,
        // continue waiting for the next client
        if (client_fd == INVALID_SOCKET_VALUE) {
            continue;
        }


        // Store the client socket and backend pool
        // so they can be passed to the client thread
        ClientThreadArg* arg =
            new ClientThreadArg{
                client_fd,
                &pool
            };


        // Handle the client request in a separate thread
        // This allows multiple clients to be handled concurrently
        spawn_detached_thread(
            client_thread_runner,
            arg
        );
    }


    // Cleanup
    // This code is unreachable because of the infinite loop,
    // but it is kept as good practice
    close_socket(server_fd);
    cleanup_sockets();

    return 0;
}

/*                 main()
                   │
                   ▼
          Parse command line
                   │
                   ▼
          Initialize sockets
                   │
                   ▼
            Create pool
                   │
                   ▼
          Parse backends
                   │
                   ▼
       Start health-check thread
                   │
                   ▼
            socket()
                   │
                   ▼
             setsockopt()
                   │
                   ▼
              bind()
                   │
                   ▼
             listen()
                   │
                   ▼
             accept()
                   │
                   ▼
       ┌───────────┴───────────┐
       │                       │
   Client 1                 Client 2
       │                       │
    Thread 1                Thread 2
       │                       │
       ▼                       ▼
 handle_client()          handle_client()
       │                       │
       └───────────┬───────────┘
                   ▼
              ServerPool
                   │
             Round Robin
                   │
       ┌───────────┼───────────┐
       ▼           ▼           ▼
   Backend 1   Backend 2   Backend 3*/
