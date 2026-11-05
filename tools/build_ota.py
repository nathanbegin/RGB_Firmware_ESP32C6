#!/usr/bin/env python3
"""Wrap an ESP application in one standard Zigbee OTA upgrade-image element."""
import argparse
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
SLOT_SIZE = 0x1E0000


def identity():
    source = (ROOT / 'main/ota_client.h').read_text()
    return {key: int(re.search(rf'^#define APP_OTA_{key}\s+(0x[0-9a-fA-F]+|\d+)', source, re.M)[1], 0)
            for key in ('MANUFACTURER', 'IMAGE_TYPE', 'FILE_VERSION')}


def wrap(data, ids):
    if not data or data[0] != 0xE9:
        raise ValueError('Expected an ESP application .bin (magic 0xE9)')
    if len(data) > SLOT_SIZE:
        raise ValueError('Application exceeds the 1.875 MiB OTA slot')
    header = struct.pack('<IHHHHHIH32sI', 0x0BEEF11E, 0x0100, 56, 0,
                         ids['MANUFACTURER'], ids['IMAGE_TYPE'], ids['FILE_VERSION'],
                         2, b'ESP-C6-UNIT02', 56 + 6 + len(data))
    return header + struct.pack('<HI', 0, len(data)) + data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build')
    args = parser.parse_args()
    ids = identity()
    data = wrap(args.binary.read_bytes(), ids)
    args.output.mkdir(parents=True, exist_ok=True)
    name = f"ESP-C6-UNIT02-{ids['FILE_VERSION']:08x}.ota"
    (args.output / name).write_bytes(data)
    (args.output / 'ota-index.json').write_text(json.dumps([{
        'manufacturerCode': ids['MANUFACTURER'], 'imageType': ids['IMAGE_TYPE'],
        'fileVersion': ids['FILE_VERSION'], 'fileSize': len(data), 'url': name,
    }], indent=2) + '\n')
    # Initialize only otadata during the first USB migration; never erase NVS/Zigbee.
    (args.output / 'ota_data_initial.bin').write_bytes(b'\xff' * 0x2000)
    print(f'Created {name}: {len(data)} bytes')


if __name__ == '__main__':
    main()
