#include "load_balancer.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

// Mutex implementation
Mutex::Mutex() {
#ifdef _WIN32
    InitializeCriticalSection(&cs);
#else
    pthread_mutex_init(&m, nullptr);
#endif
}

Mutex::~Mutex() {
#ifdef _WIN32
    DeleteCriticalSection(&cs);
#else
    pthread_mutex_destroy(&m);
#endif
}

void Mutex::lock() {
#ifdef _WIN32
    EnterCriticalSection(&cs);
#else
    pthread_mutex_lock(&m);
#endif
}

void Mutex::unlock() {
#ifdef _WIN32
    LeaveCriticalSection(&cs);
#else
    pthread_mutex_unlock(&m);
#endif
}

// LockGuard implementation
LockGuard::LockGuard(Mutex& m) : mtx(m) {
    mtx.lock();
}

LockGuard::~LockGuard() {
    mtx.unlock();
}

// Threading helpers
void spawn_detached_thread(void (*func)(void*), void* arg) {
#ifdef _WIN32
    HANDLE h = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, nullptr);
    if (h) CloseHandle(h);
#else
    pthread_t t;
    if (pthread_create(&t, nullptr, (void* (*)(void*))func, arg) == 0) {
        pthread_detach(t);
    }
#endif
}

void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

// Thread runners
void client_thread_runner(void* arg) {
    ClientThreadArg* c_arg = (ClientThreadArg*)arg;
    handle_client(c_arg->client_sock, *(c_arg->pool));
    delete c_arg;
}

void health_check_thread_runner(void* arg) {
    ServerPool* pool = (ServerPool*)arg;
    health_checker_loop(*pool);
}

// Backend implementation
void Backend::set_alive(bool is_alive) {
    LockGuard lock(mux);
    alive = is_alive;
}

bool Backend::is_alive() const {
    LockGuard lock(mux);
    return alive;
}

// ServerPool implementation
void ServerPool::add_backend(std::shared_ptr<Backend> backend) {
    backends.push_back(backend);
}

size_t ServerPool::get_size() const {
    return backends.size();
}

std::shared_ptr<Backend> ServerPool::get_next_peer() {
    if (backends.empty()) return nullptr;
    
    size_t next = current.fetch_add(1) % backends.size();
    size_t total = backends.size();

    for (size_t i = 0; i < total; ++i) {
        size_t idx = (next + i) % total;
        if (backends[idx]->is_alive()) {
            return backends[idx];
        }
    }
    return nullptr;
}

void ServerPool::mark_backend_status(const std::string& original_url, bool alive) {
    for (auto& b : backends) {
        if (b->original_url == original_url) {
            b->set_alive(alive);
            break;
        }
    }
}

const std::vector<std::shared_ptr<Backend>>& ServerPool::get_backends() const {
    return backends;
}

// Helper utilities
bool parse_backend_url(const std::string& url_str, std::string& host, int& port) {
    std::string temp = url_str;
    if (temp.compare(0, 7, "http://") == 0) {
        temp = temp.substr(7);
    } else if (temp.compare(0, 8, "https://") == 0) {
        temp = temp.substr(8);
    }

    size_t slash_pos = temp.find('/');
    if (slash_pos != std::string::npos) {
        temp = temp.substr(0, slash_pos);
    }

    size_t colon_pos = temp.find(':');
    if (colon_pos != std::string::npos) {
        host = temp.substr(0, colon_pos);
        try {
            port = std::stoi(temp.substr(colon_pos + 1));
        } catch (...) {
            return false;
        }
    } else {
        host = temp;
        port = 80;
    }
    return !host.empty();
}

bool is_backend_alive(const std::string& host, int port) {
    socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET_VALUE) return false;

#ifdef _WIN32
    DWORD timeout = 2000; // 2 seconds timeout
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    struct addrinfo hints, *res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        close_socket(s);
        return false;
    }

    bool alive = (connect(s, res->ai_addr, (int)res->ai_addrlen) == 0);
    freeaddrinfo(res);
    close_socket(s);
    return alive;
}

size_t get_content_length(const std::string& request) {
    size_t pos = request.find("Content-Length:");
    if (pos == std::string::npos) {
        pos = request.find("content-length:");
    }
    if (pos != std::string::npos) {
        size_t end_line = request.find("\r\n", pos);
        if (end_line != std::string::npos) {
            std::string line = request.substr(pos, end_line - pos);
            size_t colon = line.find(":");
            if (colon != std::string::npos) {
                std::string val = line.substr(colon + 1);
                // Trim spaces
                val.erase(0, val.find_first_not_of(" \t"));
                val.erase(val.find_last_not_of(" \t") + 1);
                try {
                    return std::stoul(val);
                } catch (...) {
                    return 0;
                }
            }
        }
    }
    return 0;
}

std::string modify_request(std::string request, const std::string& backend_host, int backend_port) {
    // Replace/Insert Host header
    size_t host_pos = request.find("Host: ");
    if (host_pos == std::string::npos) {
        host_pos = request.find("host: ");
    }
    if (host_pos != std::string::npos) {
        size_t end_line = request.find("\r\n", host_pos);
        if (end_line != std::string::npos) {
            std::string new_host_header = "Host: " + backend_host + ":" + std::to_string(backend_port);
            request.replace(host_pos, end_line - host_pos, new_host_header);
        }
    }

    // Force Connection: close
    size_t conn_pos = request.find("Connection: ");
    if (conn_pos == std::string::npos) {
        conn_pos = request.find("connection: ");
    }
    if (conn_pos != std::string::npos) {
        size_t end_line = request.find("\r\n", conn_pos);
        if (end_line != std::string::npos) {
            request.replace(conn_pos, end_line - conn_pos, "Connection: close");
        }
    } else {
        size_t header_end = request.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            request.insert(header_end, "\r\nConnection: close");
        }
    }
    return request;
}

// Read headers and body from client, forward, handle retries, stream response
void handle_client(socket_t client_sock, ServerPool& pool) {
    std::string request;
    char buffer[4096];
    int bytes_received = 0;
    
    // Read headers first
    while (true) {
        bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            close_socket(client_sock);
            return;
        }
        buffer[bytes_received] = '\0';
        request.append(buffer, bytes_received);
        if (request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }

    size_t header_end = request.find("\r\n\r\n");
    size_t content_len = get_content_length(request);
    size_t body_received = request.length() - (header_end + 4);
    
    while (body_received < content_len) {
        int to_read = (int)std::min((size_t)sizeof(buffer) - 1, content_len - body_received);
        bytes_received = recv(client_sock, buffer, to_read, 0);
        if (bytes_received <= 0) break;
        request.append(buffer, bytes_received);
        body_received += bytes_received;
    }

    int max_attempts = 3;
    bool success = false;
    
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        auto backend = pool.get_next_peer();
        if (!backend) {
            break;
        }

        socket_t backend_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (backend_sock == INVALID_SOCKET_VALUE) {
            continue;
        }

#ifdef _WIN32
        DWORD timeout = 4000; // 4s timeout
        setsockopt(backend_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(backend_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
        struct timeval tv;
        tv.tv_sec = 4;
        tv.tv_usec = 0;
        setsockopt(backend_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(backend_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

        struct addrinfo hints, *res = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        std::string port_str = std::to_string(backend->port);
        if (getaddrinfo(backend->host.c_str(), port_str.c_str(), &hints, &res) != 0) {
            close_socket(backend_sock);
            backend->set_alive(false);
            continue;
        }

        if (connect(backend_sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
            freeaddrinfo(res);
            close_socket(backend_sock);
            backend->set_alive(false);
            std::cout << "[LB] Backend " << backend->original_url << " connection failed (attempt " << attempt << "). Retrying...\n";
            continue;
        }
        freeaddrinfo(res);

        std::string modified_req = modify_request(request, backend->host, backend->port);

        int total_sent = 0;
        int req_len = (int)modified_req.length();
        bool send_error = false;
        while (total_sent < req_len) {
            int sent = send(backend_sock, modified_req.c_str() + total_sent, req_len - total_sent, 0);
            if (sent <= 0) {
                send_error = true;
                break;
            }
            total_sent += sent;
        }

        if (send_error) {
            close_socket(backend_sock);
            backend->set_alive(false);
            continue;
        }

        // Stream response back to client
        char resp_buf[8192];
        bool stream_error = false;
        while (true) {
            int resp_bytes = recv(backend_sock, resp_buf, sizeof(resp_buf), 0);
            if (resp_bytes < 0) {
                stream_error = true;
                break;
            }
            if (resp_bytes == 0) {
                break; // EOF
            }

            int client_sent = 0;
            bool client_write_error = false;
            while (client_sent < resp_bytes) {
                int sent = send(client_sock, resp_buf + client_sent, resp_bytes - client_sent, 0);
                if (sent <= 0) {
                    client_write_error = true;
                    break;
                }
                client_sent += sent;
            }
            if (client_write_error) {
                break;
            }
        }

        close_socket(backend_sock);
        if (!stream_error) {
            success = true;
            break;
        }
    }

    if (!success) {
        std::string error_resp = "HTTP/1.1 503 Service Unavailable\r\n"
                                 "Content-Type: text/plain\r\n"
                                 "Content-Length: 24\r\n"
                                 "Connection: close\r\n\r\n"
                                 "Service Not Available\r\n";
        send(client_sock, error_resp.c_str(), (int)error_resp.length(), 0);
    }

    close_socket(client_sock);
}

void health_checker_loop(ServerPool& pool) {
    while (true) {
        sleep_ms(15000); // Check every 15 seconds
        std::cout << "[LB] Health Check Triggered:\n";
        for (auto& b : pool.get_backends()) {
            bool alive = is_backend_alive(b->host, b->port);
            b->set_alive(alive);
            std::cout << "  - Backend " << b->original_url << " is " << (alive ? "UP" : "DOWN") << "\n";
        }
    }
}
