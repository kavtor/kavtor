"""Exercise the touch HTTP bridge against an actual local panel peer."""
import importlib.util
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import socket
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('touch_surface', ROOT / 'tools/run_touch_surface.py')
surface = importlib.util.module_from_spec(spec)
spec.loader.exec_module(surface)


def wait_for(predicate):
    deadline = time.monotonic() + 3
    while not predicate():
        if time.monotonic() > deadline:
            raise AssertionError('Peer did not reach expected state')
        time.sleep(.01)


class TouchSurfaceTest(unittest.TestCase):
    def setUp(self):
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen()
        self.calls = []
        self.peer = None
        self.stop = threading.Event()
        self.state = {'event': 'state', 'connected': True, 'me': 0, 'dustRatio': 50, 'capabilities': {'touchPreparation': True, 'touchIndependentMe': True}}
        self.thread = threading.Thread(target=self.receive, daemon=True)
        self.thread.start()
        self.bridge = surface.PanelBridge('127.0.0.1', self.listener.getsockname()[1])
        wait_for(lambda: self.bridge.snapshot()['state'])
        self.http = ThreadingHTTPServer(('127.0.0.1', 0), surface.handler(self.bridge, ROOT / 'share/touch'))
        self.http_thread = threading.Thread(target=self.http.serve_forever, daemon=True)
        self.http_thread.start()
        self.url = f'http://127.0.0.1:{self.http.server_port}'

    def receive(self):
        self.peer, _ = self.listener.accept()
        try:
            self.send(self.state)
            buffer = b''
            while not self.stop.is_set():
                part = self.peer.recv(65536)
                if not part:
                    break
                buffer += part
                while b'\n' in buffer:
                    raw, buffer = buffer.split(b'\n', 1)
                    request = json.loads(raw)
                    self.calls.append(request)
                    if request['cmd'] == 'catalog':
                        self.send({'event': 'catalog', 'patterns': [{'id': 'bar', 'sony': 1}]})
                    elif request['cmd'] == 'key_processing':
                        self.send({'event': 'ack', 'cmd': 'key_processing'})
                    elif request['cmd'] == 'dust_params':
                        if request['ratio'] == 101:
                            self.send({'event': 'error', 'cmd': 'dust_params', 'message': 'Invalid ratio'})
                        else:
                            self.state['dustRatio'] = request['ratio']
                            self.send(self.state)
                            # An unrelated ACK must not satisfy the waiting command.
                            self.send({'event': 'ack', 'cmd': 'rate'})
                            self.send({'event': 'ack', 'cmd': 'dust_params'})
        except OSError:
            pass

    def send(self, message):
        self.peer.sendall(json.dumps(message).encode() + b'\n')

    def request(self, command, origin=None):
        headers = {'Content-Type': 'application/json'}
        if origin:
            headers['Origin'] = origin
        request = Request(self.url + '/api/command', json.dumps(command).encode(), headers)
        try:
            with urlopen(request, timeout=6) as response:
                return response.status, json.load(response)
        except HTTPError as error:
            with error:
                return error.code, json.load(error)

    def tearDown(self):
        self.http.shutdown()
        self.http.server_close()
        self.bridge.close()
        self.stop.set()
        if self.peer:
            self.peer.close()
        self.listener.close()
        self.thread.join(3)
        self.http_thread.join(3)

    def test_state_assets_and_ack(self):
        with urlopen(self.url + '/api/state') as response:
            state = json.load(response)
        self.assertTrue(state['transportConnected'])
        self.assertEqual(state['state']['dustRatio'], 50)
        for path, kind in (('/', 'text/html'), ('/surface.js', 'text/javascript'), ('/surface.css', 'text/css'), ('/icon.svg','image/svg+xml')):
            with urlopen(self.url + path) as response:
                self.assertTrue(response.headers['Content-Type'].startswith(kind))
                self.assertIn("script-src 'self'", response.headers['Content-Security-Policy'])
        code, reply = self.request({'cmd': 'dust_params', 'ratio': 75, 'size': 2, 'flash': 0})
        self.assertEqual(code, 200)
        self.assertEqual(reply['cmd'], 'dust_params')
        self.assertEqual(self.bridge.snapshot()['state']['dustRatio'], 75)
        snapshot = self.bridge.snapshot()
        snapshot['state']['dustRatio'] = 0
        self.assertEqual(self.bridge.snapshot()['state']['dustRatio'], 75)

    def test_rejection_is_not_a_commit(self):
        code, reply = self.request({'cmd': 'dust_params', 'ratio': 101, 'size': 2, 'flash': 0})
        self.assertEqual(code, 409)
        self.assertEqual(reply['message'], 'Invalid ratio')
        self.assertEqual(self.bridge.snapshot()['state']['dustRatio'], 50)

    def test_no_program_takes_or_cross_origin_commands(self):
        self.assertEqual(self.request({'cmd': 'cut'})[0], 400)
        self.assertFalse(any(call['cmd'] == 'cut' for call in self.calls))
        self.assertEqual(self.request({'cmd': 'dust_params', 'ratio': 75}, 'https://unrelated.invalid')[0], 403)
        self.assertFalse(any(call['cmd'] == 'dust_params' for call in self.calls))

    def test_browser_cannot_disable_on_air_guard(self):
        code, reply = self.request({'cmd': 'key_processing', 'target': 'key', 'slot': 0,
                                    'settings': {'mode': 'linear'}, 'guardOnAir': False})
        self.assertEqual(code, 200)
        self.assertEqual(reply['cmd'], 'key_processing')
        self.assertTrue(next(call for call in self.calls if call['cmd'] == 'key_processing')['guardOnAir'])

    def test_touch_cannot_change_panel_delegation(self):
        self.assertEqual(self.request({'cmd':'me','slot':2})[0],400)
        code,_=self.request({'cmd':'key_processing','target':'key','slot':0,'targetMe':2,'settings':{'mode':'linear'}})
        self.assertEqual(code,200)
        self.assertEqual(self.bridge.snapshot()['state']['me'],0)
        self.assertEqual(next(c for c in self.calls if c['cmd']=='key_processing')['targetMe'],2)

    def test_disconnect_removes_stale_state(self):
        self.peer.shutdown(socket.SHUT_RDWR)
        wait_for(lambda: not self.bridge.snapshot()['transportConnected'])
        self.assertEqual(self.bridge.snapshot()['state'], {})
        self.assertEqual(self.request({'cmd': 'dust_params', 'ratio': 75})[0], 503)


if __name__ == '__main__':
    unittest.main()
