#!/usr/bin/env python3
"""
Automated master test harness for PolyCache.
Runs unit tests, launches the live server, and executes integration, protocol, and recovery test suites.
"""
import subprocess
import time
import sys
import os
import signal

def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    test_runner_bin = os.path.join(root_dir, "build", "test_runner.exe" if os.name == "nt" else "test_runner")
    server_bin = os.path.join(root_dir, "build", "test_server.exe" if os.name == "nt" else "polycache-server")
    if not os.path.exists(server_bin):
        server_bin = os.path.join(root_dir, "build", "polycache-server.exe" if os.name == "nt" else "polycache-server")

    print("==================================================================")
    print("           PolyCache Automated Test Verification                  ")
    print("==================================================================")

    # 1. Run C++ Unit Tests
    print("\n--- 1. Executing C++ Unit Tests ---")
    if not os.path.exists(test_runner_bin):
        print(f"Error: test runner binary not found at {test_runner_bin}")
        sys.exit(1)

    unit_proc = subprocess.run([test_runner_bin], cwd=root_dir)
    if unit_proc.returncode != 0:
        print("[-] C++ Unit Tests Failed!")
        sys.exit(unit_proc.returncode)

    # 2. Launch PolyCache Server on Port 6389
    test_port = 6389
    aof_test_file = os.path.join(root_dir, "test_live.aof")
    if os.path.exists(aof_test_file):
        os.remove(aof_test_file)

    print(f"\n--- 2. Starting PolyCache Server (port {test_port}) ---")
    server_cmd = [
        server_bin,
        "--port", str(test_port),
        "--aof", "yes",
        "--aof-file", aof_test_file,
        "--fsync", "everysec"
    ]
    server_proc = subprocess.Popen(server_cmd, cwd=root_dir)
    time.sleep(1.0) # Allow server to bind and start event loop

    try:
        # 3. Run Integration Tests
        print("\n--- 3. Running Integration Test Suite ---")
        integration_script = os.path.join(root_dir, "tests", "integration", "test_server_integration.py")
        subprocess.run([sys.executable, integration_script, str(test_port)], check=True)

        # 4. Run Protocol Edge Cases
        print("\n--- 4. Running Protocol Edge Cases Suite ---")
        protocol_script = os.path.join(root_dir, "tests", "protocol", "test_protocol_edgecases.py")
        subprocess.run([sys.executable, protocol_script, str(test_port)], check=True)

        # 5. Run Recovery & BGREWRITEAOF Tests
        print("\n--- 5. Running Recovery & AOF Rewrite Suite ---")
        recovery_script = os.path.join(root_dir, "tests", "recovery", "test_recovery.py")
        subprocess.run([sys.executable, recovery_script, str(test_port)], check=True)

        # 6. Run Stress Test
        print("\n--- 6. Running Concurrent Connection Stress Test ---")
        stress_script = os.path.join(root_dir, "scripts", "stress_test.py")
        subprocess.run([sys.executable, stress_script, "100", str(test_port)], check=True)

        # 7. Run Performance Benchmark
        print("\n--- 7. Running Performance Benchmark ---")
        bench_script = os.path.join(root_dir, "benchmarks", "benchmark.py")
        subprocess.run([sys.executable, bench_script, "--port", str(test_port), "-n", "10000", "-c", "5", "-P", "16"], check=True)

        print("\n==================================================================")
        print("    [SUCCESS] ALL PolyCache TEST SUITES PASSED FLAWLESSLY!        ")
        print("==================================================================")

    finally:
        print("\nStopping PolyCache server...")
        server_proc.terminate()
        try:
            server_proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server_proc.kill()
        
        # Cleanup temporary test files
        for f in [aof_test_file, "temp-rewrite.aof"]:
            if os.path.exists(f):
                try: os.remove(f)
                except: pass

if __name__ == "__main__":
    main()
