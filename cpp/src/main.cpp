#include "load_balancer.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>

int main(int argc, char* argv[]) {
    std::string backends_str;
    int port = 3030;

    // Command line argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--backends" || arg == "-backends") && i + 1 < argc) {
            backends_str = argv[++i];
        } else if ((arg == "--port" || arg == "-port") && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "Invalid port number. Using default 3030.\n";
            }
        }
    }

    if (backends_str.empty()) {
        std::cerr << "Error: Please provide one or more backends to load balance.\n"
                  << "Usage: simplelb --backends <url1,url2,...> [--port <port>]\n"
                  << "Example: simplelb --backends http://127.0.0.1:3031,http://127.0.0.1:3032 --port 3030\n";
        return 1;
    }

    // Initialize platform sockets
    init_sockets();

    ServerPool pool;
    std::stringstream ss(backends_str);
    std::string token;
    
    std::cout << "Configured backends:\n";
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        
        auto backend = std::make_shared<Backend>();
        backend->original_url = token;
        
        if (parse_backend_url(token, backend->host, backend->port)) {
            pool.add_backend(backend);
            std::cout << "  - " << backend->host << ":" << backend->port 
                      << " (parsed from " << token << ")\n";
        } else {
            std::cerr << "  - Failed to parse backend URL: " << token << "\n";
        }
    }

    if (pool.get_size() == 0) {
        std::cerr << "Error: No valid backends parsed. Exiting.\n";
        cleanup_sockets();
        return 1;
    }

    // Start background health checking thread
    spawn_detached_thread(health_check_thread_runner, &pool);

    // Create listening socket
    socket_t server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCKET_VALUE) {
        std::cerr << "Error: Failed to create socket.\n";
        cleanup_sockets();
        return 1;
    }

    // Allow socket reuse
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Error: Bind failed on port " << port << ".\n";
        close_socket(server_fd);
        cleanup_sockets();
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Error: Listen failed.\n";
        close_socket(server_fd);
        cleanup_sockets();
        return 1;
    }

    std::cout << "\nC++ Load Balancer started at http://127.0.0.1:" << port << "\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        socket_t client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_fd == INVALID_SOCKET_VALUE) {
            continue;
        }

        // Handle the request in a separate thread
        ClientThreadArg* arg = new ClientThreadArg{client_fd, &pool};
        spawn_detached_thread(client_thread_runner, arg);
    }

    // Cleanup (unreachable in infinite loop but good practice)
    close_socket(server_fd);
    cleanup_sockets();
    return 0;
}
