#!/usr/bin/env python3
"""Serve kavtor's touch preparation surface through its authoritative panel API."""
import argparse
import copy
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import socket
import threading
import time
from urllib.parse import urlsplit

PREPARATION_COMMANDS = frozenset({
    'me', 'rate', 'wipe_settings', 'wipe_style', 'wipe_pattern', 'wipe_dir',
    'wipe_edge', 'wipe_border_profile', 'dust_params', 'mix_params', 'dip_color',
    'key_processing', 'dme_background',
})


class PanelBridge:
    def __init__(self, host, port):
        self.address = host, port
        self.condition = threading.Condition()
        self.command_lock = threading.Lock()
        self.stop = threading.Event()
        self.socket = None
        self.state = {}
        self.catalogue = []
        self.waiting = None
        self.reply = None
        self.thread = threading.Thread(target=self._receive, daemon=True)
        self.thread.start()

    def _receive(self):
        while not self.stop.is_set():
            connection = None
            try:
                connection = socket.create_connection(self.address, 2)
                connection.settimeout(2)
                with self.condition:
                    self.socket = connection
                    self.state = {}
                connection.sendall(b'{"cmd":"catalog"}\n')
                buffer = b''
                while not self.stop.is_set():
                    try:
                        part = connection.recv(65536)
                    except socket.timeout:
                        continue
                    if not part:
                        break
                    buffer += part
                    if len(buffer) > 2 * 1024 * 1024:
                        raise ValueError('Panel message exceeds surface limit')
                    while b'\n' in buffer:
                        raw, buffer = buffer.split(b'\n', 1)
                        message = json.loads(raw)
                        if not isinstance(message, dict):
                            continue
                        with self.condition:
                            if message.get('event') == 'state':
                                self.state = message
                            elif message.get('event') == 'catalog':
                                self.catalogue = message.get('patterns', message.get('wipes', []))
                            if message.get('event') in ('ack', 'error') and message.get('cmd') == self.waiting:
                                self.reply = message
                                self.condition.notify_all()
            except (OSError, ValueError):
                pass
            finally:
                with self.condition:
                    self.socket = None
                    self.state = {}
                    self.catalogue = []
                    if self.waiting:
                        self.reply = {'event': 'error', 'message': 'SERVER LOST; command outcome may be unknown'}
                    self.condition.notify_all()
                if connection:
                    connection.close()
            self.stop.wait(.5)

    def snapshot(self):
        with self.condition:
            return {'transportConnected': self.socket is not None,
                    'state': copy.deepcopy(self.state), 'catalogue': copy.deepcopy(self.catalogue)}

    def request(self, command):
        if not isinstance(command, dict) or command.get('cmd') not in PREPARATION_COMMANDS:
            raise ValueError('This surface accepts preparation commands only')
        command = dict(command)
        if command['cmd'] == 'key_processing':
            command['guardOnAir'] = True
        with self.command_lock, self.condition:
            if self.socket is None or not self.state:
                raise ConnectionError('SERVER LOST')
            if not self.state.get('capabilities', {}).get('touchPreparation', False):
                raise ValueError('Touch preparation requires kavtor 0.26.0 or newer')
            self.waiting, self.reply = command['cmd'], None
            try:
                self.socket.sendall(json.dumps(command, allow_nan=False).encode() + b'\n')
                deadline = time.monotonic() + 5
                while self.reply is None:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        # Close the session so a late ACK cannot satisfy a later command.
                        self.socket.shutdown(socket.SHUT_RDWR)
                        raise TimeoutError('No confirmation; refresh state before retrying')
                    self.condition.wait(remaining)
                return copy.deepcopy(self.reply)
            finally:
                self.waiting = None

    def close(self):
        self.stop.set()
        with self.condition:
            if self.socket:
                try:
                    self.socket.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        self.thread.join(3)


def handler(bridge, root):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def send_json(self, value, status=200):
            body = json.dumps(value).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urlsplit(self.path).path
            if path == '/api/state':
                self.send_json(bridge.snapshot())
            elif path in ('/', '/index.html', '/surface.js', '/surface.css'):
                name = 'index.html' if path == '/' else path[1:]
                body = (root / name).read_bytes()
                self.send_response(200)
                self.send_header('Content-Type', {'index.html': 'text/html; charset=utf-8', 'surface.js': 'text/javascript; charset=utf-8', 'surface.css': 'text/css; charset=utf-8'}[name])
                self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:")
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control', 'no-cache')
                self.end_headers()
                self.wfile.write(body)
            else:
                self.send_json({'error': 'Not found'}, 404)

        def do_POST(self):
            if self.path != '/api/command':
                self.send_json({'error': 'Not found'}, 404)
                return
            origin = self.headers.get('Origin')
            if origin and urlsplit(origin).netloc != self.headers.get('Host'):
                self.send_json({'error': 'Cross-origin command rejected'}, 403)
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 8192:
                    raise ValueError('Invalid request size')
                self.connection.settimeout(5)
                command = json.loads(self.rfile.read(size))
                reply = bridge.request(command)
                self.send_json(reply, 200 if reply.get('event') == 'ack' else 409)
            except (ValueError, UnicodeError) as exc:
                self.send_json({'error': str(exc)}, 400)
            except (OSError, TimeoutError) as exc:
                self.send_json({'error': str(exc)}, 503)
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', default='127.0.0.1')
    parser.add_argument('--panel-port', type=int, default=9100)
    parser.add_argument('--listen', default='127.0.0.1', help='Use 0.0.0.0 for access from a tablet on your LAN')
    parser.add_argument('--port', type=int, default=8098)
    args = parser.parse_args()
    if not 1 <= args.panel_port <= 65535 or not 0 <= args.port <= 65535:
        parser.error('Ports must be in range (panel: 1–65535; HTTP: 0–65535)')
    bridge = PanelBridge(args.server, args.panel_port)
    prefix = Path(__file__).resolve().parents[1]
    roots = (prefix / 'share' / 'touch', prefix / 'share' / 'kavtor' / 'touch')
    root = next((path for path in roots if (path / 'index.html').is_file()), None)
    if root is None:
        parser.error('Touch assets not found; install share/kavtor/touch or run from the source tree')
    server = ThreadingHTTPServer((args.listen, args.port), handler(bridge, root))
    print(f'kavtor touch preparation: http://{args.listen}:{server.server_port}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        bridge.close()


if __name__ == '__main__':
    main()
