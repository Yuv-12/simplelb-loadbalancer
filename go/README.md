# Go SimpleLB Load Balancer

A high-performance L7 HTTP load balancer built using Go's built-in `net/http` and `httputil.ReverseProxy` standard library modules.

## Features
- **Concurrent**: Leverages Go's built-in goroutines for highly performant request handling.
- **Round-Robin Routing**: Distributes incoming HTTP requests cyclicly among backend servers.
- **Active Health Checks**: Periodically monitors backend availability on a ticker goroutine.
- **Failover & Connection Retries**: Automatically retry alternative backends up to 3 times if a request fails, marking the dead server as inactive.
- **Zero-Dependency**: Uses standard Go SDK only.

## Running the Go Load Balancer

Ensure you have [Go](https://go.dev/) installed:

```bash
go run main.go --backends http://localhost:3031,http://localhost:3032 --port 3030
```

- `--backends`: Comma-separated list of backend URLs (e.g., `http://localhost:3031`).
- `--port`: The port that the load balancer listens on (default is `3030`).
