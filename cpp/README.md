# C++ SimpleLB Load Balancer

A high-performance, multithreaded L7 HTTP load balancer built using C++ standard library components and low-level socket API.

## Features
- **Multithreading**: Handles connections concurrently on separate threads.
- **Round-Robin Routing**: Distributes incoming HTTP requests evenly among healthy backends.
- **Active TCP Health Checks**: Periodically pings backends on a separate thread to maintain host availability.
- **Failover & Connection Retries**: Automatically detects if a selected backend fails during request setup and retries on alternative backends up to 3 times before returning a `503 Service Unavailable`.
- **Zero-Dependency**: Uses raw standard library and OS sockets (Winsock on Windows, POSIX on Unix).

## Build Instructions

### Option 1: Direct g++ Compilation (Quickest)
If you have GCC/g++ installed, run the following command inside the `cpp/` directory:

**Windows (PowerShell/CMD):**
```powershell
g++ -O3 -std=c++11 src/main.cpp src/load_balancer.cpp -o simplelb.exe -lws2_32
```

**Linux/macOS:**
```bash
g++ -O3 -std=c++11 src/main.cpp src/load_balancer.cpp -o simplelb -lpthread
```

### Option 2: CMake (Standard)
If you have CMake installed, build from the `cpp/` directory:
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Running the C++ Load Balancer

Once compiled, start the load balancer with the `--backends` and `--port` parameters:
```bash
./simplelb.exe --backends http://127.0.0.1:3031,http://127.0.0.1:3032,http://127.0.0.1:3033 --port 3030
```

- `--backends`: Comma-separated list of backend URLs (e.g., `http://127.0.0.1:3031`).
- `--port`: The port that the load balancer listens on (default is `3030`).
