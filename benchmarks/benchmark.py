#!/usr/bin/env python3
"""
PolyCache Performance Benchmark Tool.
Measures GET/SET throughput, p50/p95/p99 latencies, concurrency, and pipeline efficiency.
"""
import socket
import time
import threading
import argparse
import statistics

def format_resp_cmd(*args):
    out = f"*{len(args)}\r\n"
    for a in args:
        sa = str(a)
        out += f"${len(sa.encode('utf-8'))}\r\n{sa}\r\n"
    return out.encode("utf-8")

def worker_thread(host, port, num_requests, pipeline_size, cmd_type, latencies, worker_id):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.connect((host, port))

    ops_done = 0
    batch_latencies = []

    while ops_done < num_requests:
        batch_count = min(pipeline_size, num_requests - ops_done)
        batch_bytes = b""
        for i in range(batch_count):
            k = f"bench_key_{worker_id}_{ops_done + i}"
            if cmd_type == "SET":
                batch_bytes += format_resp_cmd("SET", k, "v" * 32)
            elif cmd_type == "GET":
                batch_bytes += format_resp_cmd("GET", k)
            elif cmd_type == "INCR":
                batch_bytes += format_resp_cmd("INCR", f"counter_{worker_id}")
            elif cmd_type == "PING":
                batch_bytes += format_resp_cmd("PING")

        t0 = time.perf_counter()
        s.sendall(batch_bytes)
        
        # Read all expected responses for the batch
        remaining = batch_count
        while remaining > 0:
            chunk = s.recv(16384)
            if not chunk: break
            if cmd_type in ("SET", "PING"):
                remaining -= chunk.count(b"\r\n")
            elif cmd_type == "GET":
                # Count bulk string headers
                remaining -= chunk.count(b"$")
            else:
                remaining -= chunk.count(b"\r\n")

        t1 = time.perf_counter()
        elapsed_us = (t1 - t0) * 1_000_000 / batch_count
        for _ in range(batch_count):
            batch_latencies.append(elapsed_us)
        ops_done += batch_count

    s.close()
    latencies.extend(batch_latencies)

def run_benchmark(host="127.0.0.1", port=6379, total_requests=50000, clients=10, pipeline=16, cmd="SET"):
    print("================================================================")
    print(f"  PolyCache Benchmark: {cmd} workload")
    print(f"  Target: {host}:{port}")
    print(f"  Clients: {clients} concurrent | Pipeline batch: {pipeline}")
    print(f"  Total Requests: {total_requests}")
    print("================================================================")

    # Pre-populate keys for GET
    if cmd == "GET":
        init_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        init_sock.connect((host, port))
        batch = b""
        for i in range(1000):
            batch += format_resp_cmd("SET", f"bench_key_0_{i}", "prepopulated_val")
        init_sock.sendall(batch)
        init_sock.recv(16384)
        init_sock.close()

    reqs_per_client = total_requests // clients
    threads = []
    latencies = []

    t_start = time.perf_counter()

    for cid in range(clients):
        t = threading.Thread(
            target=worker_thread,
            args=(host, port, reqs_per_client, pipeline, cmd, latencies, cid)
        )
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    t_end = time.perf_counter()
    duration = t_end - t_start
    total_ops = len(latencies)
    ops_per_sec = total_ops / duration if duration > 0 else 0

    latencies.sort()
    p50 = latencies[int(0.50 * total_ops)] if latencies else 0
    p95 = latencies[int(0.95 * total_ops)] if latencies else 0
    p99 = latencies[int(0.99 * total_ops)] if latencies else 0

    print(f"\nResults for {cmd}:")
    print(f"  Throughput:      {ops_per_sec:,.0f} ops/sec")
    print(f"  Elapsed Time:    {duration:.3f} seconds")
    print(f"  Total Requests:  {total_ops}")
    print(f"  Latency p50:     {p50:.1f} us")
    print(f"  Latency p95:     {p95:.1f} us")
    print(f"  Latency p99:     {p99:.1f} us")
    print("----------------------------------------------------------------\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PolyCache Performance Benchmark")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("-n", "--requests", type=int, default=20000, help="Total requests")
    parser.add_argument("-c", "--clients", type=int, default=10, help="Concurrent clients")
    parser.add_argument("-P", "--pipeline", type=int, default=16, help="Pipeline batch size")
    parser.add_argument("-t", "--command", default="SET", choices=["SET", "GET", "INCR", "PING"])
    args = parser.parse_args()

    run_benchmark(
        host=args.host,
        port=args.port,
        total_requests=args.requests,
        clients=args.clients,
        pipeline=args.pipeline,
        cmd=args.command
    )
