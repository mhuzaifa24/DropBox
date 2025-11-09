#!/usr/bin/env python3
# test/test_client.py
# Simple interactive client automation for Phase1 tests.

import socket
import time
import sys

HOST = "127.0.0.1"
PORT = 8080

def recv_all(sock, timeout=0.2):
    sock.settimeout(timeout)
    out = b""
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            out += data
    except socket.timeout:
        pass
    except Exception:
        pass
    return out.decode(errors="ignore")

def send_line(sock, line, sleep=0.05):
    if not line.endswith("\n"):
        line = line + "\n"
    sock.sendall(line.encode())
    time.sleep(sleep)
    return recv_all(sock)

def run_test():
    sock = socket.create_connection((HOST, PORT))
    print("Connected to server")
    print(recv_all(sock))

    # 1) Try signup
    print("SIGNUP ->", send_line(sock, "SIGNUP testuser testpass"))

    # 2) Login
    print("LOGIN ->", send_line(sock, "LOGIN testuser testpass"))

    # 3) UPLOAD small file (single chunk)
    text = "hello-phase1-dropbox\n"
    print("UPLOAD ->", send_line(sock, "UPLOAD myfile.txt"))
    # server should say READY or similar; send file bytes
    sock.sendall(text.encode())
    time.sleep(0.05)
    print("After send file ->", recv_all(sock))

    # Give worker time
    time.sleep(0.3)

    # 4) LIST
    print("LIST ->", send_line(sock, "LIST"))

    # 5) DOWNLOAD
    print("DOWNLOAD ->", send_line(sock, "DOWNLOAD myfile.txt"))
    # server may send the file directly as TCP payload; receive it
    time.sleep(0.2)
    print("Downloaded payload ->", recv_all(sock))

    # 6) DELETE
    print("DELETE ->", send_line(sock, "DELETE myfile.txt"))
    time.sleep(0.1)

    # 7) LIST again
    print("LIST2 ->", send_line(sock, "LIST"))

    # 8) QUIT
    print("QUIT ->", send_line(sock, "QUIT"))
    sock.close()
    print("Test client done.")

if __name__ == "__main__":
    run_test()
