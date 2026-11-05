import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('build_ota', ROOT / 'tools/build_ota.py')
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


class PackageTests(unittest.TestCase):
    def test_standard_wire_format(self):
        payload = b'\xe9' + bytes(range(256))
        ids = ota.identity()
        data = ota.wrap(payload, ids)
        header = struct.unpack('<IHHHHHIH32sI', data[:56])
        self.assertEqual(header[:4], (0x0BEEF11E, 0x100, 56, 0))
        self.assertEqual(header[4:7], (ids['MANUFACTURER'], ids['IMAGE_TYPE'], ids['FILE_VERSION']))
        self.assertEqual(header[-1], len(data))
        self.assertEqual(struct.unpack('<HI', data[56:62]), (0, len(payload)))
        self.assertEqual(data[62:], payload)

    def test_invalid_or_oversize_input(self):
        for payload in (b'', b'not firmware', b'\xe9' * (ota.SLOT_SIZE + 1)):
            with self.assertRaises(ValueError):
                ota.wrap(payload, ota.identity())

    def test_partition_layout(self):
        parts = {}
        def number(s):
            return int(s[:-1], 0) * 1024 if s.endswith('K') else int(s, 0)
        for line in (ROOT / 'partitions.csv').read_text().splitlines():
            if line.startswith('#') or not line.strip():
                continue
            name, kind, subtype, offset, size, *_ = [s.strip() for s in line.split(',')]
            parts[name] = (number(offset), number(size))
        for name, expected in {'nvs': (0x9000, 0x6000), 'zb_storage': (0x3D0000, 0x4000), 'zb_fct': (0x3D4000, 0x400)}.items():
            self.assertEqual(parts[name], expected)
        for name in ('ota_0', 'ota_1'):
            self.assertEqual(parts[name][0] % 0x10000, 0)
            self.assertEqual(parts[name][1], ota.SLOT_SIZE)
        end = 0x9000
        for start, size in sorted(parts.values()):
            self.assertGreaterEqual(start, end)
            end = start + size
        self.assertLessEqual(end, 4 * 1024 * 1024)


if __name__ == '__main__':
    unittest.main()
