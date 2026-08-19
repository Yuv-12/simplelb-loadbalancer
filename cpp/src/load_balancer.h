#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

// This handles different socket libraries for Windows and Linux
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

    // Initializes the Windows socket library
    #define init_sockets() { WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData); }

    // Cleans up the Windows socket library
    #define cleanup_sockets() WSACleanup()

    // Closes a socket
    #define close_socket(s) closesocket(s)

    #define INVALID_SOCKET_VALUE INVALID_SOCKET

    // socket_t is used so the same socket type can be used on both Windows and Linux
    typedef SOCKET socket_t;

#else

    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <pthread.h>

    // On Linux socket is represented by an integer
    typedef int socket_t;

    // Linux does not require explicit socket initialization
    #define init_sockets()

    #define cleanup_sockets()

    // Closes a socket on Linux
    #define close_socket(s) close(s)

    #define INVALID_SOCKET_VALUE -1

#endif

#include <string>
#include <vector>
#include <atomic>
#include <memory>


// Cross-platform Mutex and LockGuard
// Mutex is used to make shared data thread safe
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

    // lock() locks the critical section
    void lock();

    // unlock() releases the critical section
    void unlock();
};


// LockGuard automatically locks the mutex when created
// and unlocks it when it goes out of scope
class LockGuard {
private:
    Mutex& mtx;

public:
    LockGuard(Mutex& m);

    // Destructor releases the lock
    ~LockGuard();
};


// Represents a backend server
struct Backend {

    std::string host;
    int port;

    // Stores the original URL given by the user
    // Example: http://127.0.0.1:3031
    std::string original_url;

    // Initially the backend is considered alive
    bool alive = true;

    // Mutex protects the alive status from multiple threads
    mutable Mutex mux;

    // Changes the alive status of the backend
    void set_alive(bool is_alive);

    // Returns whether the backend is currently alive
    bool is_alive() const;
};


// Thread-safe pool of backend servers
class ServerPool {
private:

    // Stores all the backend servers
    std::vector<std::shared_ptr<Backend>> backends;

    // Keeps track of which backend should be selected next
    // atomic makes it safe when multiple threads access it
    std::atomic<size_t> current{0};

public:

    // Adds a backend to the pool
    void add_backend(std::shared_ptr<Backend> backend);

    // Returns the total number of backends
    size_t get_size() const;

    // Round Robin selection algorithm
    // Returns the next healthy backend
    std::shared_ptr<Backend> get_next_peer();

    // Updates the status of a backend
    void mark_backend_status(
        const std::string& original_url,
        bool alive
    );

    // Returns all the backends in the pool
    const std::vector<std::shared_ptr<Backend>>& get_backends() const;
};


// Threading helpers

// Creates a new detached thread
void spawn_detached_thread(void (*func)(void*), void* arg);

// Pauses the current thread for the given number of milliseconds
void sleep_ms(int ms);


// Thread argument structures

// Stores the information required by the client thread
struct ClientThreadArg {

    // Socket used to communicate with the client
    socket_t client_sock;

    // Pointer to the backend pool
    ServerPool* pool;
};


// Helper utilities

// Parses the backend URL and extracts the host and port
// Example: http://127.0.0.1:3031
// host = 127.0.0.1
// port = 3031
bool parse_backend_url(
    const std::string& url_str,
    std::string& host,
    int& port
);


// Checks whether the backend server is reachable
// Connection successful -> true
// Connection failed -> false
bool is_backend_alive(
    const std::string& host,
    int port
);


// Modifies the client request before forwarding it to the backend
// It updates headers such as Host and Connection
std::string modify_request(
    std::string request,
    const std::string& backend_host,
    int backend_port
);


// Determines the size of the HTTP request body
// It reads the Content-Length header
size_t get_content_length(
    const std::string& request
);


// Connection handling & loop functions

// This is the heart of the load balancer
// It receives the client request,
// selects a backend,
// forwards the request,
// and sends the backend response back to the client
void handle_client(
    socket_t client_sock,
    ServerPool& pool
);


// Continuously checks whether the backend servers are alive
// The health check runs periodically
void health_checker_loop(
    ServerPool& pool
);


// Thread runner wrappers

// Starts the client handling function inside a thread
void client_thread_runner(void* arg);


// Starts the health checker inside a separate thread
void health_check_thread_runner(void* arg);

#endif // LOAD_BALANCER_H
