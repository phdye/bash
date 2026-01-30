#!/usr/bin/env python3
"""
Simple event listener for bash event_server loadable module.
Listens on a Unix domain socket and prints received events.

Usage:
    python3 event_listener.py /tmp/bash_events.sock
"""

import socket
import sys
import os
import json

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <socket_path>")
        sys.exit(1)
    
    socket_path = sys.argv[1]
    
    # Remove socket if it exists
    if os.path.exists(socket_path):
        os.unlink(socket_path)
    
    # Create Unix domain socket (DGRAM for UDP-style)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(socket_path)
    
    print(f"Listening on {socket_path}")
    print("Press Ctrl+C to stop\n")
    
    try:
        while True:
            data, addr = sock.recvfrom(8192)
            try:
                event = json.loads(data.decode('utf-8'))
                event_type = event.get('type', 'unknown')
                command = event.get('command', '')
                exit_status = event.get('exit_status', 0)
                cwd = event.get('cwd', '')
                
                if event_type == 'pre_command':
                    print(f">>> {command}")
                    if cwd:
                        print(f"    (cwd: {cwd})")
                elif event_type == 'post_command':
                    status_str = f"[exit {exit_status}]" if exit_status != 0 else "[ok]"
                    print(f"<<< {command} {status_str}")
                else:
                    print(f"??? {event}")
            except json.JSONDecodeError:
                print(f"Raw: {data.decode('utf-8', errors='replace')}")
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        sock.close()
        if os.path.exists(socket_path):
            os.unlink(socket_path)

if __name__ == '__main__':
    main()
