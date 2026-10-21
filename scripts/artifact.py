#!/usr/bin/env python3
"""Validate the install bundle and copy a release artifact with checksum."""
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
package = json.loads((root / 'package.json').read_text())
source = root / 'build' / (root.name + '.pbw')
with zipfile.ZipFile(source) as bundle:
    assert bundle.testzip() is None, 'Damaged PBW'
    app = json.loads(bundle.read('appinfo.json'))
    assert app['uuid'] == package['pebble']['uuid'], 'Wrong UUID'
    assert app['shortName'] == 'Inspect', 'Wrong display name'
    assert app['targetPlatforms'] == ['emery'], 'Wrong platforms'
    assert app['versionLabel'] == package['version'], 'Wrong watch version'
    assert app['watchapp']['watchface'] is False, 'Not a watchapp'
    assert 'emery/pebble-app.bin' in bundle.namelist(), 'Missing Emery binary'
    assert not any('pebble-js-app' in x or 'worker' in x for x in bundle.namelist()), 'Unexpected companion/worker'
destination = root / 'artifacts' / f"pebble-inspect-{package['version']}.pbw"
destination.parent.mkdir(exist_ok=True)
shutil.copyfile(source, destination)
digest = hashlib.sha256(destination.read_bytes()).hexdigest()
destination.with_suffix('.pbw.sha256').write_text(f'{digest}  {destination.name}\n')
inputs = [root / 'package.json', root / 'wscript'] + sorted((root / 'src').rglob('*')) + sorted((root / 'resources').rglob('*'))
manifest = {'artifact': destination.name, 'sha256': digest,
            'bytes': destination.stat().st_size, 'uuid': app['uuid'],
            'version': app['versionLabel'], 'platforms': app['targetPlatforms'],
            'pebble_tool': '5.0.40', 'sdk': '4.33.1',
            'input_sha256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in inputs if p.is_file()}}
(destination.parent / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'{destination}: {destination.stat().st_size} bytes\nSHA-256: {digest}')
