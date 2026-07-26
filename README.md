# SimpleLB - Multi-Language Load Balancer Project

Welcome to **SimpleLB**, a robust, educational multi-language L7 HTTP Load Balancer project featuring high-performance implementations in both **C++** and **Python**.

This repository is designed to demonstrate low-level systems programming (sockets, multithreading, concurrency) in C++ alongside modern asynchronous networking in Python.

---

## Project Structure

```
simplelb/
├── cpp/
│   ├── src/
│   │   ├── main.cpp        # CLI Parser & socket listener entry point
│   │   ├── load_balancer.h # Platform sockets and thread wrappers
│   │   └── load_balancer.cpp # HTTP routing, active health checks, retry logic
│   ├── CMakeLists.txt      # Cross-platform CMake configuration
│   └── README.md           # C++ Specific setup guide
├── python/
│   ├── load_balancer.py    # Asynchronous L7 load balancer using asyncio
│   └── README.md           # Python Specific setup guide
├── tools/
│   ├── mock_backend.py     # Multithreaded test HTTP server (multiple ports)
│   └── benchmark.py        # Client traffic simulator & metrics analyzer
├── docker-compose.yml      # Multi-instance deployment configuration
├── LICENSE                 # MIT License
└── README.md               # Main project overview (this file)
```

---

## Core Features & Logic

Both the **C++** and **Python** implementations include the following features:

1. **Round-Robin Routing**: Distributes incoming HTTP requests in a cyclic, atomic manner across all healthy backend servers.
2. **Active TCP Health Checks**: A background worker thread/task periodically connects to each backend to verify its health status (UP or DOWN), taking unhealthy servers out of rotation.
3. **Failover & Connection Retries**: If a selected backend fails to respond or accept a connection, the load balancer automatically flags it as DOWN and retries alternative backends up to 3 times before sending a `503 Service Unavailable` response to the client.
4. **Zero-Dependencies**: Both versions require only standard compiler toolchains and built-in runtimes (no complex third-party library installations required).

---

## Getting Started

### 1. Start the Mock Backends
Start the backend mock servers using Python. This starts 4 HTTP servers listening on ports `3031`, `3032`, `3033`, and `3034` in a single command:
```bash
python tools/mock_backend.py --ports 3031,3032,3033,3034
```

### 2. Run the Load Balancers (Choose One)

#### A. C++ Load Balancer
Compile using any standard C++ compiler.
* **Windows (MinGW/GCC)**:
  ```powershell
  g++ -O3 -std=c++11 cpp/src/main.cpp cpp/src/load_balancer.cpp -o simplelb_cpp.exe -lws2_32
  ./simplelb_cpp.exe --backends http://127.0.0.1:3031,http://127.0.0.1:3032,http://127.0.0.1:3033,http://127.0.0.1:3034 --port 3030
  ```
* **Linux/macOS**:
  ```bash
  g++ -O3 -std=c++11 cpp/src/main.cpp cpp/src/load_balancer.cpp -o simplelb_cpp -lpthread
  ./simplelb_cpp --backends http://127.0.0.1:3031,http://127.0.0.1:3032,http://127.0.0.1:3033,http://127.0.0.1:3034 --port 3030
  ```

#### B. Python Load Balancer
Run the async load balancer with:
```bash
python python/load_balancer.py --backends http://127.0.0.1:3031,http://127.0.0.1:3032,http://127.0.0.1:3033,http://127.0.0.1:3034 --port 3030
```

---

## Benchmarking & Verifying

We can verify load distribution, throughput, and error rates using the included benchmarking tool.

While the load balancer is running on port `3030` and the mock backends are running, execute:
```bash
python tools/benchmark.py --url http://127.0.0.1:3030 --requests 200 --concurrency 10
```

### Example Benchmark Output:
```
========================================
               RESULTS                
========================================
Elapsed Time:         0.08 seconds
Throughput:           2500.00 req/sec
Average Latency:      3.10 ms
P50 Latency:          2.80 ms
P90 Latency:          4.50 ms
P99 Latency:          6.20 ms

HTTP Status Codes:
  200: 200 (100.0%)

Traffic Distribution (Backend IDs/Ports hit):
  Backend 3031: 50 (25.0%)
  Backend 3032: 50 (25.0%)
  Backend 3033: 50 (25.0%)
  Backend 3034: 50 (25.0%)
========================================
```

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
