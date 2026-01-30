#!/usr/bin/env python3
"""
Secure Event Client for bash event_server loadable module.

This client can:
1. Receive events from bash (command execution monitoring)
2. Send commands to bash (secure command injection)

Usage:
    # Receive events only
    python3 event_client.py /tmp/bash.sock
    
    # Interactive mode (receive events + send commands)
    python3 event_client.py /tmp/bash.sock --interactive --auth mytoken

Protocol:
    Events socket:   /tmp/bash.sock      (bash -> client, events)
    Commands socket: /tmp/bash.sock.cmd  (client -> bash, commands)
"""

import socket
import sys
import os
import json
import argparse
import threading
import uuid
import time
from datetime import datetime

class EventClient:
    def __init__(self, socket_path, auth_token=None):
        self.socket_path = socket_path
        self.cmd_socket_path = socket_path + ".cmd"
        self.auth_token = auth_token
        self.event_socket = None
        self.cmd_socket = None
        self.running = False
        self.pending_responses = {}
        
    def start_event_listener(self):
        """Start listening for events from bash."""
        # Remove existing socket if present
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)
        
        self.event_socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.event_socket.bind(self.socket_path)
        self.event_socket.settimeout(0.5)  # Allow periodic checks
        
        print(f"[*] Listening for events on {self.socket_path}")
        
    def start_cmd_socket(self):
        """Create socket for sending commands."""
        self.cmd_socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        # Bind to a unique path for receiving responses
        self.response_path = f"/tmp/bash_client_{os.getpid()}.sock"
        if os.path.exists(self.response_path):
            os.unlink(self.response_path)
        self.cmd_socket.bind(self.response_path)
        self.cmd_socket.settimeout(5.0)
        
        print(f"[*] Command socket ready (responses on {self.response_path})")
        
    def format_event(self, event):
        """Format an event for display."""
        event_type = event.get('type', 'unknown')
        command = event.get('command', '')
        exit_status = event.get('exit_status', 0)
        cwd = event.get('cwd', '')
        injected = event.get('injected', False)
        timestamp = event.get('timestamp', 0)
        
        # Convert nanosecond timestamp to datetime
        ts = datetime.fromtimestamp(timestamp / 1e9) if timestamp else datetime.now()
        ts_str = ts.strftime('%H:%M:%S.%f')[:-3]
        
        inject_marker = " [INJECTED]" if injected else ""
        
        if event_type == 'pre_command':
            return f"[{ts_str}] >>> {command}{inject_marker}"
        elif event_type == 'post_command':
            status = f"[exit {exit_status}]" if exit_status != 0 else "[ok]"
            return f"[{ts_str}] <<< {command} {status}{inject_marker}"
        elif event_type == 'result':
            req_id = event.get('id', '?')
            status = event.get('status', '?')
            exit_code = event.get('exit_code', -1)
            message = event.get('message', '')
            return f"[{ts_str}] RESULT id={req_id} status={status} exit={exit_code} msg={message}"
        elif event_type == 'error':
            req_id = event.get('id', '?')
            code = event.get('code', '?')
            message = event.get('message', '')
            return f"[{ts_str}] ERROR id={req_id} code={code} msg={message}"
        else:
            return f"[{ts_str}] {event_type}: {json.dumps(event)}"
    
    def receive_events(self, callback=None):
        """Receive and process events (blocking loop)."""
        self.running = True
        
        while self.running:
            try:
                data, addr = self.event_socket.recvfrom(8192)
                try:
                    event = json.loads(data.decode('utf-8'))
                    formatted = self.format_event(event)
                    print(formatted)
                    
                    if callback:
                        callback(event)
                        
                except json.JSONDecodeError:
                    print(f"[?] Raw: {data.decode('utf-8', errors='replace')}")
                    
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"[!] Error receiving event: {e}")
                break
    
    def send_command(self, command, mode="subshell", timeout=10.0):
        """Send a command to bash for execution."""
        if not self.auth_token:
            print("[!] No auth token configured. Use --auth <token>")
            return None
        
        if not self.cmd_socket:
            self.start_cmd_socket()
        
        # Generate unique request ID
        req_id = str(uuid.uuid4())[:8]
        
        # Build request
        request = {
            "type": "execute",
            "id": req_id,
            "auth": self.auth_token,
            "command": command,
            "mode": mode
        }
        
        try:
            # Send to bash's command socket
            self.cmd_socket.sendto(
                json.dumps(request).encode('utf-8'),
                self.cmd_socket_path
            )
            print(f"[>] Sent command: {command} (id={req_id})")
            
            # Wait for response
            start = time.time()
            while time.time() - start < timeout:
                try:
                    data, addr = self.cmd_socket.recvfrom(8192)
                    response = json.loads(data.decode('utf-8'))
                    
                    if response.get('id') == req_id:
                        return response
                    else:
                        # Response for different request, store it
                        self.pending_responses[response.get('id')] = response
                        
                except socket.timeout:
                    continue
                    
            print(f"[!] Timeout waiting for response (id={req_id})")
            return None
            
        except FileNotFoundError:
            print(f"[!] Command socket not found: {self.cmd_socket_path}")
            print("[!] Make sure bash has: event_server socket {path}")
            return None
        except Exception as e:
            print(f"[!] Error sending command: {e}")
            return None
    
    def interactive_mode(self):
        """Run interactive mode with command input."""
        print("\n[*] Interactive mode. Commands:")
        print("    !cmd <command>  - Send command to bash")
        print("    !status         - Request status")
        print("    !quit           - Exit")
        print("    (other input is ignored)\n")
        
        # Start event listener in background thread
        event_thread = threading.Thread(target=self.receive_events, daemon=True)
        event_thread.start()
        
        try:
            while True:
                try:
                    line = input()
                except EOFError:
                    break
                
                if line.startswith('!quit'):
                    break
                elif line.startswith('!cmd '):
                    cmd = line[5:].strip()
                    if cmd:
                        response = self.send_command(cmd)
                        if response:
                            print(f"[<] {self.format_event(response)}")
                elif line.startswith('!status'):
                    response = self.send_command("event_server status")
                    if response:
                        print(f"[<] {self.format_event(response)}")
                        
        except KeyboardInterrupt:
            pass
        finally:
            self.running = False
            
    def cleanup(self):
        """Clean up sockets."""
        self.running = False
        
        if self.event_socket:
            self.event_socket.close()
            if os.path.exists(self.socket_path):
                os.unlink(self.socket_path)
                
        if self.cmd_socket:
            self.cmd_socket.close()
            if hasattr(self, 'response_path') and os.path.exists(self.response_path):
                os.unlink(self.response_path)


def main():
    parser = argparse.ArgumentParser(
        description='Secure event client for bash event_server',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Listen for events only
  %(prog)s /tmp/bash.sock
  
  # Interactive mode with command injection
  %(prog)s /tmp/bash.sock --interactive --auth mysecrettoken
  
  # Send a single command
  %(prog)s /tmp/bash.sock --auth mysecrettoken --exec "echo hello"
        """)
    
    parser.add_argument('socket_path', help='Unix socket path (e.g., /tmp/bash.sock)')
    parser.add_argument('--auth', '-a', help='Authentication token for command injection')
    parser.add_argument('--interactive', '-i', action='store_true',
                        help='Interactive mode (events + command input)')
    parser.add_argument('--exec', '-e', dest='execute',
                        help='Execute a single command and exit')
    parser.add_argument('--mode', '-m', choices=['subshell', 'mainshell'],
                        default='subshell', help='Execution mode (default: subshell)')
    
    args = parser.parse_args()
    
    client = EventClient(args.socket_path, args.auth)
    
    try:
        if args.execute:
            # Single command execution
            client.start_cmd_socket()
            response = client.send_command(args.execute, args.mode)
            if response:
                print(client.format_event(response))
                sys.exit(0 if response.get('status') == 'ok' else 1)
            else:
                sys.exit(1)
                
        elif args.interactive:
            # Interactive mode
            client.start_event_listener()
            client.interactive_mode()
            
        else:
            # Event listener only
            client.start_event_listener()
            print("[*] Press Ctrl+C to stop\n")
            client.receive_events()
            
    except KeyboardInterrupt:
        print("\n[*] Shutting down...")
    finally:
        client.cleanup()


if __name__ == '__main__':
    main()
