#!/usr/bin/env python3
"""Exercise ONLY an already-running isolated Inspect emulator. No physical link.

Run with pebble-tool's Python after `make emulator`. Injected states below are
emulator test inputs, never compiled into the application. Screenshots are
emulator evidence, not physical battery or Bluetooth measurements.
"""
import json
from pathlib import Path
import re
import struct
import time
from uuid import UUID
import zlib

from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.websocket import WebsocketTransport
from libpebble2.communication.transports.qemu.protocol import QemuButton, QemuBattery, QemuBluetoothConnection
from libpebble2.protocol.apps import AppRunState, AppRunStateStart, AppRunStateRequest
from libpebble2.protocol.logs import AppLogMessage, AppLogShippingControl
from libpebble2.services.screenshot import Screenshot
from pebble_tool.commands.emucontrol import send_data_to_qemu

root = Path(__file__).resolve().parents[1]
info = json.loads((root / '.emulator/tmp/pb-emulator.json').read_text())['emery']['4.33.1']
port = int(info['pypkjs']['port'])
package = json.loads((root / 'package.json').read_text())
app_uuid = UUID(package['pebble']['uuid'])
pebble = PebbleConnection(WebsocketTransport(f'ws://127.0.0.1:{port}/'))
pebble.connect()
pebble.run_async()
logs = []
def logged(packet):
    if packet.uuid == app_uuid:
        logs.append({'timestamp': packet.timestamp, 'level': packet.level, 'message': packet.message})
pebble.register_endpoint(AppLogMessage, logged)
pebble.send_packet(AppLogShippingControl(enable=True))
captures = root / 'docs/screenshots'
captures.mkdir(exist_ok=True)

def click(button, count=1):
    for _ in range(count):
        send_data_to_qemu(pebble.transport, QemuButton(state=getattr(QemuButton.Button, button.title())))
        time.sleep(.12)
        send_data_to_qemu(pebble.transport, QemuButton(state=0))
        time.sleep(.45 if button in ('select', 'back') else .22)

def capture(name):
    time.sleep(.2)
    rows = Screenshot(pebble).grab_image()
    assert len(rows) == 228 and len(rows[0]) == 600, 'Unexpected Emery dimensions'
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    raw = b''.join(b'\0' + row for row in rows)
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 200, 228, 8, 2, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    (captures / (name + '.png')).write_bytes(png)
    print('Captured', name, flush=True)

def scroll_end(direction):
    previous = None
    for _ in range(160):
        click(direction, 4)
        rows = Screenshot(pebble).grab_image()
        body = b''.join(rows[92:208])
        if body == previous:
            return
        previous = body
    raise AssertionError('Scroll boundary was not reached')

selected = 0
def launch():
    global selected
    pebble.send_packet(AppRunState(data=AppRunStateStart(uuid=app_uuid)))
    time.sleep(1.2)
    selected = 0

def assert_exited():
    time.sleep(.5)
    response = pebble.send_and_read(AppRunState(data=AppRunStateRequest()), AppRunState)
    assert not (isinstance(response.data, AppRunStateStart) and response.data.uuid == app_uuid), 'Back did not exit Inspect'

def page(index):
    global selected
    click('select')
    if index > selected:
        click('down', index - selected)
    elif index < selected:
        click('up', selected - index)
    click('select')
    selected = index

# App installation normally leaves Overview open. Use the real Back flow to exit.
time.sleep(1.2)  # Installation returns before the launch animation is finished.
click('back', 2)
assert_exited()
launch()
capture('overview')
click('select')
capture('menu')
click('select')
for index, name in enumerate(('overview', 'battery', 'connection', 'memory', 'device', 'capabilities')):
    page(index)
    capture(name)
    click('down', 4)
    capture(name + '-scrolled')
    scroll_end('down')
    capture(name + '-bottom')
    scroll_end('up')

# Actual emulator service events, with no time acceleration or app test mode.
send_data_to_qemu(pebble.transport, QemuBattery(percent=70, charging=True))
time.sleep(.5)
page(1)
capture('battery-charging')
send_data_to_qemu(pebble.transport, QemuBattery(percent=70, charging=False))
page(2)
send_data_to_qemu(pebble.transport, QemuBluetoothConnection(connected=False))
print('Waiting 30s for the firmware disconnect debounce', flush=True)
time.sleep(30)
capture('connection-disconnected')
click('down', 5)
capture('connection-scope')
click('down', 5)
capture('connection-events')
send_data_to_qemu(pebble.transport, QemuBluetoothConnection(connected=True))
time.sleep(1)

for cycle in range(8):
    page(3)
    page(0)
    click('back', 2)
    time.sleep(.25)
    assert_exited()
    launch()
page(2)
capture('connection-after-reopen')
click('down', 3)
capture('connection-after-reopen-counts')
page(3)
capture('memory-after-reopen')
page(0)
steady_heap = []
for _ in range(6):
    page(3)
    page(0)
    time.sleep(.2)
    values = [int(m.group(1)) for row in logs if (m := re.search(r'Navigation heap used=(\d+)', row['message']))]
    steady_heap.append(values[-1])
time.sleep(.5)
entries = [int(m.group(1)) for row in logs if (m := re.search(r'Inspect entry heap=(\d+)', row['message']))]
navigations = [int(m.group(1)) for row in logs if (m := re.search(r'Navigation heap used=(\d+)', row['message']))]
exit_remaining = [int(m.group(1)) for row in logs if (m := re.search(r'Still allocated <(\d+)B>', row['message']))]
report = {'scope': 'Emery emulator only; service states injected by this script',
          'entry_heap_bytes': entries, 'navigation_heap_bytes': sorted(set(navigations)),
          'steady_navigation_heap_bytes': steady_heap, 'exit_allocated_bytes': exit_remaining,
          'entry_cycles': 9, 'screens_checked': 6, 'disconnect_wait_seconds': 30, 'logs': logs}
(root / 'artifacts/emulator-smoke.json').write_text(json.dumps(report, indent=2) + '\n')
assert len(entries) >= 8, f'Missing entry logs: {entries}'
assert max(entries) - min(entries) <= 32, f'Entry heap growth: {entries}'
assert len(exit_remaining) >= 8 and all(x == 0 for x in exit_remaining), 'Exit leak reported'
# Different pages and menu animations have different transient allocation sizes.
# Compare the same checkpoint after warm-up, not every unlike UI state.
assert max(steady_heap[1:]) - min(steady_heap[1:]) <= 32, 'Steady navigation heap growth'
assert not [row for row in logs if row['level'] <= 50], 'Inspect warning/error logs; inspect evidence'
print('Emulator smoke passed; entry heap:', entries, 'navigation heap:', sorted(set(navigations)), flush=True)
pebble.send_packet(AppLogShippingControl(enable=False))
pebble.transport.ws.close()
