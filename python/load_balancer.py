import argparse
import asyncio
import urllib.parse

# The backend class convert the backend into three important pieces of information
class Backend:
    def __init__(self, original_url):#constructor it receives URL
        self.original_url = original_url # store the original URL
        parsed = urllib.parse.urlparse(original_url) # The url is parsed parsed by user is stored in backend 
        #urlparse() breaks the url ino different components
        #scheme = http
        #hostname= localhost
        #port = 8080
        if not parsed.scheme: # handles url without scheme(a scheme is http:// or https://)
            parsed = urllib.parse.urlparse("http://" + original_url)
        self.host = parsed.hostname or '127.0.0.1' # gives the host name if no host name then default value is used
        self.port = parsed.port or 80 # gives the url has port number if no value then default port is 80(standard port for HTTP)
        self.alive = True #Mark the backend is alive

    def __str__(self): # This controls what happen when we print the object
        return f"{self.host}:{self.port}"

class ServerPool:
    # constructor
    def __init__(self):
        self.backends = [] #stores alll backend server
        self.current = 0 #This tells the load balancer where to start searching for the next backend
        self.lock = asyncio.Lock()# The lock ensures the two coroutine don't simultaneously modify self.current and accidently select the same backend

    #Adding a backend
    def add_backend(self, backend):
        self.backends.append(backend)

    #Round Robin selection algorithm
    async def get_next_peer(self):
        async with self.lock: #Lock ensures only one coroutine can select/update the next backend
            if not self.backends: #If there are no server return None
                return None
            
            total = len(self.backends)#total number of backend
            #search for a healthy backend
            for i in range(total): 
                idx = (self.current + i) % total # "%" total creates the circular/Round robin behavior
                backend = self.backends[idx] #Check whether backend is alive 
                if backend.alive:
                    self.current = (idx + 1) % total # move to the next backend
                    return backend
            return None

#backend health checking
#This server checks whether a backend server is reachable
async def is_backend_alive(host, port):
    try:
        #It attempt to establish TCP connection
        _, writer = await asyncio.wait_for(
            asyncio.open_connection(host, port),
            timeout=2.0
        )
        #if connection succeeds, the backend is considered alive
        writer.close()
        await writer.wait_closed()
        return True
        #Then the connection is closed and True is returned
    except Exception:
        return False
    #Connection successful -> up
    #connection failed      -> DOWN
async def health_checker_loop(pool):
    while True:
        await asyncio.sleep(15)    #Every 15 seconds the load balancer perform the health checks
        print("[LB] Health Check Triggered:")
        for backend in pool.backends: #it checks every backend
            alive = await is_backend_alive(backend.host, backend.port)
            backend.alive = alive
            status = "UP" if alive else "DOWN"
            print(f"  - Backend {backend.original_url} is {status}")
#This function determines how large the HTTP request body is
#It is necessary to determine content length because the loadbalancer needs to know how many more bytes should i read?
def get_content_length(request_bytes):
    headers_part = request_bytes.split(b'\r\n\r\n')[0]
    for line in headers_part.split(b'\r\n'):
        if line.lower().startswith(b'content-length:'):
            try:
                return int(line.split(b':')[1].strip())
            except ValueError:
                return 0
    return 0
# THis modify the client request before forwarding it
def modify_request(request_bytes, backend):
    try:
        request = request_bytes.decode('utf-8')
    except UnicodeDecodeError:
        return request_bytes

    lines = request.split('\r\n')
    for i, line in enumerate(lines):
        if line.lower().startswith('host:'):
            lines[i] = f"Host: {backend.host}:{backend.port}"#this is what loadbalancer forwards this makes the request appropriate for the selected backend
            break
    #it aso changes connection:..... to connection: close
    #if the header doesn,t exist, it add one
    #This simplifies connection management because the backend connection is closed after the response
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

#This is the heart of load balancer
async def handle_client(reader, writer, pool): #reader->used to receive data from the client and Writer-> used to send data back to the client
   #Step 1: Read the HTTP request
    request = b''# initially the request is empty
    while True:
        data = await reader.read(4096) #It reads data upto 4096 bytes
        if not data:
            break
        request += data
         #The loop stops when
        if b'\r\n\r\n' in request: # an empty line which tells that the header ends here
            break

    if not request:
        writer.close()
        await writer.wait_closed()
        return

    header_end = request.find(b'\r\n\r\n')
    content_len = get_content_length(request)#read the request body after the header is received
    body_received = len(request) - (header_end + 4)

    while body_received < content_len:
        data = await reader.read(min(4096, content_len - body_received))
        if not data:
            break
        request += data
        body_received += len(data)
# Retry mechanism
    max_attempts = 3 #so the load balancer can try up to three backend selections
    success = False
    #for each attempt 
    for attempt in range(1, max_attempts + 1):
        backend = await pool.get_next_peer()
        if not backend: #if no healthy backend exist
            break

        try: #connect to the selected backend
            backend_reader, backend_writer = await asyncio.wait_for(
                asyncio.open_connection(backend.host, backend.port),
                timeout=4.0 # now the loadbalancer establishes a TCP connections with the maximum connection time of 4 seconds
            ) 
        except Exception: #if it fails to connect
            print(f"[LB] Backend {backend.original_url} connection failed (attempt {attempt}). Retrying...")
            backend.alive = False # the backend is immediately marked as down
            continue #then the load balancer try another backend

        try:# forwardthe request
            modified_req = modify_request(request, backend)
            #The request is modified for the selected backend
            backend_writer.write(modified_req)
            await backend_writer.drain()#sends it to client

            # receive backend response
            while True:# this loop continuously read the backend response
                resp_data = await backend_reader.read(8192)
                if not resp_data:
                    break
                writer.write(resp_data) # sends the exact response to the client so the load balancer act as proxy
                await writer.drain()

            success = True
            backend_writer.close()
            await backend_writer.wait_closed()
            break
        except Exception as e: # if backend fails then it tries the next backend this provides basic fault tolerance
            print(f"[LB] Error proxying to {backend.original_url}: {e}")
            backend.alive = False
            backend_writer.close()
            continue

    if not success: # if every backend fails
        error_resp = (
            b"HTTP/1.1 503 Service Unavailable\r\n" # the server sends
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
# start health checkerthis is important: it starts the health checker as a background asyncio task
    #so you effictively have two things concurrently
    asyncio.create_task(health_checker_loop(pool))

    #start the load balancer server
    server = await asyncio.start_server(
        lambda r, w: handle_client(r, w, pool),
        '0.0.0.0', # 0.0.0.0 means listen on all network interfaces
        args.port
    )

    print(f"\nPython Load Balancer started at http://127.0.0.1:{args.port}")
    async with server:
        await server.serve_forever() # this keeps the load balancer server runnoiing

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nShutting down Python load balancer...")


"""                 ┌─────────────────────┐
                    │      Client         │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │   Load Balancer     │
                    │      :3030          │
                    └──────────┬──────────┘
                               │
                     ┌─────────┴─────────┐
                     │    ServerPool     │
                     │                   │
                     │ Round Robin       │
                     │ Health Status     │
                     │ asyncio.Lock      │
                     └─────────┬─────────┘
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
        ┌──────────┐     ┌──────────┐     ┌──────────┐
        │ Backend 1│     │ Backend 2│     │ Backend 3│
        │   :8001  │     │   :8002  │     │   :8003  │
        └──────────┘     └──────────┘     └──────────┘
              ▲                ▲                ▲
              └────────────────┼────────────────┘
                               │
                       Health Checker
                         every 15 sec"""
