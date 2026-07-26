# Python SimpleLB Load Balancer

A lightweight, high-performance, asynchronous L7 HTTP load balancer built using Python's built-in `asyncio` framework.

## Features
- **Asynchronous**: Uses cooperative multitasking (`asyncio`) to handle thousands of concurrent requests efficiently on a single thread.
- **Round-Robin Routing**: Distributes traffic evenly among healthy servers.
- **Active TCP Health Checks**: Periodically pings backends asynchronously to detect failures and recoveries.
- **Failover & Connection Retries**: Automatically detects failed backend connections and retries alternative backends up to 3 times before returning a `503 Service Unavailable`.
- **Zero-Dependency**: Standard library only (no pip dependencies required).

## Running the Python Load Balancer

Run the load balancer directly with Python:
```bash
python load_balancer.py --backends http://127.0.0.1:3031,http://127.0.0.1:3032,http://127.0.0.1:3033 --port 3030
```

- `--backends`: Comma-separated list of backend URLs (e.g., `http://127.0.0.1:3031`).
- `--port`: The port that the load balancer listens on (default is `3030`).
