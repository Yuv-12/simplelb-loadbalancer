#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600 // Expose getaddrinfo on Windows Vista and later
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    typedef int socklen_t;
    #define init_sockets() { WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData); }
    #define cleanup_sockets() WSACleanup()
    #define close_socket(s) closesocket(s)
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    typedef SOCKET socket_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <pthread.h>
    typedef int socket_t;
    #define init_sockets()
    #define cleanup_sockets()
    #define close_socket(s) close(s)
    #define INVALID_SOCKET_VALUE -1
#endif

#include <string>
#include <vector>
#include <atomic>
#include <memory>

// Cross-platform Mutex and LockGuard to bypass C++11 <mutex> compiler compatibility issues in older MinGW
class Mutex {
private:
#ifdef _WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t m;
#endif

public:
    Mutex();
    ~Mutex();
    void lock();
    void unlock();
};

class LockGuard {
private:
    Mutex& mtx;
public:
    LockGuard(Mutex& m);
    ~LockGuard();
};

// Represents a backend server
struct Backend {
    std::string host;
    int port;
    std::string original_url; // e.g., "http://127.0.0.1:3031"
    bool alive = true;
    mutable Mutex mux;

    void set_alive(bool is_alive);
    bool is_alive() const;
};

// Thread-safe pool of backend servers
class ServerPool {
private:
    std::vector<std::shared_ptr<Backend>> backends;
    std::atomic<size_t> current{0};

public:
    void add_backend(std::shared_ptr<Backend> backend);
    size_t get_size() const;
    std::shared_ptr<Backend> get_next_peer();
    void mark_backend_status(const std::string& original_url, bool alive);
    const std::vector<std::shared_ptr<Backend>>& get_backends() const;
};

// Threading helpers
void spawn_detached_thread(void (*func)(void*), void* arg);
void sleep_ms(int ms);

// Thread argument structures
struct ClientThreadArg {
    socket_t client_sock;
    ServerPool* pool;
};

// Helper utilities
bool parse_backend_url(const std::string& url_str, std::string& host, int& port);
bool is_backend_alive(const std::string& host, int port);
std::string modify_request(std::string request, const std::string& backend_host, int backend_port);
size_t get_content_length(const std::string& request);

// Connection handling & loop functions
void handle_client(socket_t client_sock, ServerPool& pool);
void health_checker_loop(ServerPool& pool);

// Thread runner wrappers
void client_thread_runner(void* arg);
void health_check_thread_runner(void* arg);

#endif // LOAD_BALANCER_H
