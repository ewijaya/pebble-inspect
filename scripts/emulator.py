#!/usr/bin/env python3
"""Isolated, loopback-only adapter for the pinned Pebble Tool 5.0.40 CLI.

Run with pebble-tool's Python. Never changes the installed CLI or credentials.
The upstream CLI lacks bind-address options for several debug services.
"""
import importlib.metadata
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

if len(sys.argv) > 1 and sys.argv[1] == '--phone-bridge':
    # pypkjs has no listen-address flag. Restrict its wildcard bind before import.
    from gevent import monkey
    monkey.patch_all()
    from gevent import pywsgi
    original_server_init = pywsgi.WSGIServer.__init__
    def local_server_init(server, listener, *args, **kwargs):
        if isinstance(listener, tuple):
            listener = ('127.0.0.1', listener[1])
        return original_server_init(server, listener, *args, **kwargs)
    pywsgi.WSGIServer.__init__ = local_server_init
    sys.argv = ['pypkjs'] + sys.argv[2:]
    runpy.run_module('pypkjs', run_name='__main__')
    raise SystemExit

if importlib.metadata.version('pebble-tool') != '5.0.40':
    raise SystemExit('This adapter requires pebble-tool 5.0.40; review it before changing versions.')
if len(sys.argv) < 2 or sys.argv[1] not in ('install', 'screenshot', 'emu-button', 'emu-battery',
                                          'emu-bt-connection', 'logs', 'repl', 'kill', 'ping'):
    raise SystemExit('Usage: emulator.py <install|screenshot|emu-button|emu-battery|emu-bt-connection|logs|repl|kill|ping> [CLI arguments]')
if (Path.home() / '.pebble-sdk').exists():
    raise SystemExit('Legacy ~/.pebble-sdk overrides XDG isolation; use a separate user or adapt this wrapper.')
if any(x in sys.argv for x in ('--phone', '--cloudpebble', '--qemu', '--serial', '--adb')):
    raise SystemExit('This wrapper only controls the isolated emulator.')

root = Path(__file__).resolve().parents[1]
source = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'pebble-sdk/SDKs/4.33.1'
isolated = root / '.emulator/pebble-sdk'
sdks = isolated / 'SDKs'
sdks.mkdir(parents=True, exist_ok=True)
if not source.is_dir():
    raise SystemExit(f'Missing SDK: {source}')
for link, target in ((sdks / '4.33.1', source), (sdks / 'current', Path('4.33.1'))):
    if not link.exists():
        link.symlink_to(target, target_is_directory=True)
os.environ['XDG_DATA_HOME'] = str(root / '.emulator')
# The CLI process registry uses tempfile.gettempdir(), independently of XDG.
# Isolate both or a VNC-mode change can terminate somebody else's emulator.
temporary = root / '.emulator/tmp'
temporary.mkdir(exist_ok=True)
os.environ['TMPDIR'] = str(temporary)
tempfile.tempdir = str(temporary)
for key in ('PEBBLE_PHONE', 'PEBBLE_CLOUDPEBBLE', 'PEBBLE_EMULATOR', 'PEBBLE_QEMU', 'PEBBLE_QEMU_PATH'):
    os.environ.pop(key, None)

original_popen = subprocess.Popen
def local_popen(args, *pos, **kw):
    if isinstance(args, (list, tuple)):
        args = list(args)
        if Path(str(args[0])).name == 'qemu-pebble':
            args = [x.replace('tcp::', 'tcp:127.0.0.1:') if isinstance(x, str) else x for x in args]
            if '-vnc' in args:
                args[args.index('-vnc') + 1] = '127.0.0.1:1'
        elif '-m' in args and 'websockify' in args:
            args[args.index('6080')] = '127.0.0.1:6080'
        elif '-m' in args and 'pypkjs' in args:
            args = [sys.executable, str(Path(__file__).resolve()), '--phone-bridge'] + args[args.index('pypkjs') + 1:]
            if '--oauth' in args:
                raise RuntimeError('Unexpected credential in isolated emulator')
    return original_popen(args, *pos, **kw)
subprocess.Popen = local_popen

from pebble_tool import run_tool
arguments = sys.argv[1:]
if arguments[0] != 'kill':
    arguments += ['--emulator', 'emery', '--sdk', '4.33.1', '--vnc']
sys.exit(run_tool(arguments))
