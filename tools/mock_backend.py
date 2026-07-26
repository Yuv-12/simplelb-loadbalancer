import argparse
import time
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

class MockBackendHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        port = self.server.server_address[1]
        print(f"[Backend:{port}] - - {format % args}")

    def do_GET(self):
        parsed_url = urlparse(self.path)
        path = parsed_url.path
        query = parse_qs(parsed_url.query)

        if 'sleep' in query or 'delay' in query:
            try:
                ms = int(query.get('sleep', query.get('delay'))[0])
                time.sleep(ms / 1000.0)
            except (ValueError, TypeError):
                pass

        if path == '/health':
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(b"OK\n")
            return

        if path == '/fail':
            self.send_response(500)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(b"Simulated internal server error\n")
            return

        port = self.server.server_address[1]
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.end_headers()
        response_msg = f"Hello from backend running on port {port}\n"
        self.wfile.write(response_msg.encode('utf-8'))

def start_server(port):
    server = HTTPServer(('127.0.0.1', port), MockBackendHandler)
    print(f"Backend started on http://127.0.0.1:{port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

def main():
    parser = argparse.ArgumentParser(description="Multi-instance Mock HTTP Backend Server")
    parser.add_argument(
        '--ports', 
        type=str, 
        default="3031,3032,3033,3034", 
        help="Comma-separated list of ports to run backend servers on"
    )
    args = parser.parse_args()

    ports = [int(p.strip()) for p in args.ports.split(',')]
    threads = []

    print(f"Starting {len(ports)} mock backend instances...")
    for port in ports:
        t = threading.Thread(target=start_server, args=(port,), daemon=True)
        t.start()
        threads.append(t)

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nShutting down backend servers...")

if __name__ == '__main__':
    main()
