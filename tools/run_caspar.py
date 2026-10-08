#!/usr/bin/env python3
"""Start a local CasparCG server after checking its AMCP ports."""
import argparse
import fcntl
import os
from pathlib import Path
import socket
import tempfile
import xml.etree.ElementTree as ET


def cef_cache_owner(tree, directory):
    """Report an occupied CEF cache before Chromium can block server startup."""
    cache = tree.findtext('./html/cache-path', 'cef-cache').strip()
    if not cache:
        return None
    path = Path(cache)
    if not path.is_absolute():
        path = directory / path
    try:
        target = os.readlink(path / 'SingletonLock')
    except OSError:
        return None
    host, separator, pid_text = target.rpartition('-')
    if not separator or host != socket.gethostname() or not pid_text.isdigit():
        return None
    pid = int(pid_text)
    if pid <= 0:
        return None
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return None  # Stale locks are handled by Chromium itself.
    except PermissionError:
        pass  # An inaccessible live owner still prevents safe cache reuse.
    return path, pid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--directory', type=Path, default=Path.cwd(), help='Working directory for relative media/cache paths')
    args = parser.parse_args()
    binary, config, directory = args.binary.resolve(), args.config.resolve(), args.directory.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f'Server executable is unavailable: {binary}')
    if not directory.is_dir():
        parser.error(f'Working directory is unavailable: {directory}')
    try:
        tree = ET.parse(config)
        ports = sorted({int(controller.findtext('port', '5250'))
                        for controller in tree.findall('./controllers/tcp')
                        if controller.findtext('protocol', 'AMCP').strip().upper() == 'AMCP'})
    except (OSError, ValueError, ET.ParseError) as error:
        parser.error(f'Cannot read server configuration: {error}')
    if not ports or any(port < 1 or port > 65535 for port in ports):
        parser.error('Configuration must contain valid AMCP TCP ports')
    owner = cef_cache_owner(tree, directory)
    if owner:
        cache, pid = owner
        parser.exit(1, f'CEF cache {cache} is still owned by process {pid}.\n'
                    'The previous server may be stopped or still starting, even if AMCP is unavailable.\n'
                    'Resume or stop that instance before restarting. No second server was started.\n')
    # Hold the lock across exec to prevent concurrent launches by this wrapper.
    lock_dir = Path(os.environ.get('XDG_RUNTIME_DIR') or tempfile.gettempdir()) / f'kavtor-launch-{os.getuid()}'
    lock_dir.mkdir(mode=0o700, exist_ok=True)
    locks = []
    for port in ports:
        lock = open(lock_dir / f'amcp-{port}.lock', 'a')
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.exit(1, f'CasparCG is already starting or running on AMCP port {port}. No second server was started.\n')
        os.set_inheritable(lock.fileno(), True)
        locks.append(lock)
        # Also detect servers started outside this wrapper. Do this before CEF
        # touches a shared cache or any output window is created.
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                probe.bind(('0.0.0.0', port))
            except OSError as error:
                parser.exit(1, f'AMCP port {port} is unavailable ({error.strerror}).\n'
                            'CasparCG may already be running. Use that instance or stop it before restarting.\n'
                            'No second server was started.\n')
    os.chdir(directory)
    os.execv(str(binary), [str(binary), str(config)])


if __name__ == '__main__':
    main()
