import argparse
import asyncio
import urllib.parse

class Backend:
    def __init__(self, original_url):
        self.original_url = original_url
        parsed = urllib.parse.urlparse(original_url)
        if not parsed.scheme:
            parsed = urllib.parse.urlparse("http://" + original_url)
        self.host = parsed.hostname or '127.0.0.1'
        self.port = parsed.port or 80
        self.alive = True

    def __str__(self):
        return f"{self.host}:{self.port}"

class ServerPool:
    def __init__(self):
        self.backends = []
        self.current = 0
        self.lock = asyncio.Lock()

    def add_backend(self, backend):
        self.backends.append(backend)

    async def get_next_peer(self):
        async with self.lock:
            if not self.backends:
                return None
            
            total = len(self.backends)
            for i in range(total):
                idx = (self.current + i) % total
                backend = self.backends[idx]
                if backend.alive:
                    self.current = (idx + 1) % total
                    return backend
            return None

async def is_backend_alive(host, port):
    try:
        _, writer = await asyncio.wait_for(
            asyncio.open_connection(host, port),
            timeout=2.0
        )
        writer.close()
        await writer.wait_closed()
        return True
    except Exception:
        return False

async def health_checker_loop(pool):
    while True:
        await asyncio.sleep(15)
        print("[LB] Health Check Triggered:")
        for backend in pool.backends:
            alive = await is_backend_alive(backend.host, backend.port)
            backend.alive = alive
            status = "UP" if alive else "DOWN"
            print(f"  - Backend {backend.original_url} is {status}")

def get_content_length(request_bytes):
    headers_part = request_bytes.split(b'\r\n\r\n')[0]
    for line in headers_part.split(b'\r\n'):
        if line.lower().startswith(b'content-length:'):
            try:
                return int(line.split(b':')[1].strip())
            except ValueError:
                return 0
    return 0

def modify_request(request_bytes, backend):
    try:
        request = request_bytes.decode('utf-8')
    except UnicodeDecodeError:
        return request_bytes

    lines = request.split('\r\n')
    for i, line in enumerate(lines):
        if line.lower().startswith('host:'):
            lines[i] = f"Host: {backend.host}:{backend.port}"
            break
            
    conn_idx = -1
    for i, line in enumerate(lines):
        if line.lower().startswith('connection:'):
            conn_idx = i
            break
    if conn_idx >= 0:
        lines[conn_idx] = "Connection: close"
    else:
        if lines and lines[-1] == '':
            lines.insert(-1, "Connection: close")
        else:
            lines.append("Connection: close")

    return '\r\n'.join(lines).encode('utf-8')

async def handle_client(reader, writer, pool):
    request = b''
    while True:
        data = await reader.read(4096)
        if not data:
            break
        request += data
        if b'\r\n\r\n' in request:
            break

    if not request:
        writer.close()
        await writer.wait_closed()
        return

    header_end = request.find(b'\r\n\r\n')
    content_len = get_content_length(request)
    body_received = len(request) - (header_end + 4)

    while body_received < content_len:
        data = await reader.read(min(4096, content_len - body_received))
        if not data:
            break
        request += data
        body_received += len(data)

    max_attempts = 3
    success = False

    for attempt in range(1, max_attempts + 1):
        backend = await pool.get_next_peer()
        if not backend:
            break

        try:
            backend_reader, backend_writer = await asyncio.wait_for(
                asyncio.open_connection(backend.host, backend.port),
                timeout=4.0
            )
        except Exception:
            print(f"[LB] Backend {backend.original_url} connection failed (attempt {attempt}). Retrying...")
            backend.alive = False
            continue

        try:
            modified_req = modify_request(request, backend)
            backend_writer.write(modified_req)
            await backend_writer.drain()

            while True:
                resp_data = await backend_reader.read(8192)
                if not resp_data:
                    break
                writer.write(resp_data)
                await writer.drain()

            success = True
            backend_writer.close()
            await backend_writer.wait_closed()
            break
        except Exception as e:
            print(f"[LB] Error proxying to {backend.original_url}: {e}")
            backend.alive = False
            backend_writer.close()
            continue

    if not success:
        error_resp = (
            b"HTTP/1.1 503 Service Unavailable\r\n"
            b"Content-Type: text/plain\r\n"
            b"Content-Length: 24\r\n"
            b"Connection: close\r\n\r\n"
            b"Service Not Available\r\n"
        )
        try:
            writer.write(error_resp)
            await writer.drain()
        except Exception:
            pass

    writer.close()
    await writer.wait_closed()

async def main():
    parser = argparse.ArgumentParser(description="Asynchronous Python Load Balancer")
    parser.add_argument('--backends', type=str, required=True, help="Comma-separated list of backend URLs")
    parser.add_argument('--port', type=int, default=3030, help="Port to serve the load balancer on")
    args = parser.parse_args()

    pool = ServerPool()
    print("Configured backends:")
    for token in args.backends.split(','):
        token = token.strip()
        if not token:
            continue
        b = Backend(token)
        pool.add_backend(b)
        print(f"  - {b.host}:{b.port} (parsed from {token})")

    if not pool.backends:
        print("Error: No backends configured.")
        return

    asyncio.create_task(health_checker_loop(pool))

    server = await asyncio.start_server(
        lambda r, w: handle_client(r, w, pool),
        '0.0.0.0',
        args.port
    )

    print(f"\nPython Load Balancer started at http://127.0.0.1:{args.port}")
    async with server:
        await server.serve_forever()

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nShutting down Python load balancer...")
