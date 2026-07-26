import argparse
import time
import urllib.request
import urllib.error
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import Counter

def send_request(url):
    start_time = time.perf_counter()
    backend_id = "unknown"
    status_code = None
    try:
        with urllib.request.urlopen(url, timeout=5) as response:
            status_code = response.getcode()
            body = response.read().decode('utf-8', errors='ignore')
            if "port" in body:
                backend_id = body.split("port")[-1].strip()
            else:
                backend_id = "responsive-backend"
    except urllib.error.HTTPError as e:
        status_code = e.code
        backend_id = f"error-{e.code}"
    except Exception as e:
        status_code = "error"
        backend_id = "failed-connection"

    latency = (time.perf_counter() - start_time) * 1000.0
    return status_code, latency, backend_id

def main():
    parser = argparse.ArgumentParser(description="Load Balancer Benchmarking Tool")
    parser.add_argument('--url', type=str, default="http://127.0.0.1:3030", help="URL of the load balancer")
    parser.add_argument('--requests', type=int, default=100, help="Total number of requests to send")
    parser.add_argument('--concurrency', type=int, default=10, help="Number of concurrent client threads")
    args = parser.parse_args()

    print(f"Starting benchmark against {args.url}...")
    print(f"Total requests: {args.requests} | Concurrency: {args.concurrency}")

    results = []
    backends_hit = Counter()
    status_codes = Counter()

    start_time = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        futures = [executor.submit(send_request, args.url) for _ in range(args.requests)]
        
        for future in as_completed(futures):
            status_code, latency, backend_id = future.result()
            results.append(latency)
            status_codes[status_code] += 1
            backends_hit[backend_id] += 1

    total_time = time.perf_counter() - start_time

    avg_latency = sum(results) / len(results) if results else 0
    results.sort()
    p50 = results[int(len(results) * 0.50)] if results else 0
    p90 = results[int(len(results) * 0.90)] if results else 0
    p99 = results[int(len(results) * 0.99)] if results else 0

    print("\n" + "="*40)
    print("               RESULTS                ")
    print("="*40)
    print(f"Elapsed Time:         {total_time:.2f} seconds")
    print(f"Throughput:           {len(results) / total_time:.2f} req/sec")
    print(f"Average Latency:      {avg_latency:.2f} ms")
    print(f"P50 Latency:          {p50:.2f} ms")
    print(f"P90 Latency:          {p90:.2f} ms")
    print(f"P99 Latency:          {p99:.2f} ms")
    
    print("\nHTTP Status Codes:")
    for code, count in status_codes.items():
        print(f"  {code}: {count} ({count/len(results)*100:.1f}%)")

    print("\nTraffic Distribution (Backend IDs/Ports hit):")
    for backend, count in sorted(backends_hit.items()):
        print(f"  Backend {backend}: {count} ({count/len(results)*100:.1f}%)")
    print("="*40)

if __name__ == '__main__':
    main()
