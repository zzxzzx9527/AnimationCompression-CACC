"""Check packaging, frozen golden bytes, incremental reads and corruption."""
from pathlib import Path
import hashlib
import json
import sys
import unittest
import numpy as np
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'src'))
import avatar_codec as codec
from avatar_codec import low_memory, reference


def exact(a, b):
    return a.shape == b.shape and a.dtype == b.dtype == np.float32 and np.array_equal(a.view('u4'), b.view('u4'))


class CodecTests(unittest.TestCase):
    def test_frozen_golden(self):
        for record in json.loads((ROOT/'tests/fixtures/golden.json').read_text('utf-8')):
            with self.subTest(record=record['id']):
                with np.load(ROOT/'tests/fixtures'/record['target']) as archive:
                    x = archive['x']
                saved = (ROOT/'tests/fixtures'/record['stream']).read_bytes()
                self.assertEqual(hashlib.sha256(saved).hexdigest(), record['sha256'])
                body = codec.encode(x, record['names'], record['fps'], bits=record['bits'])
                self.assertEqual(body, saved)
                self.assertTrue(exact(codec.decode(saved)[0], x))
                self.assertTrue(exact(reference.decode(saved)[0], x))

    def test_stream_fragments(self):
        for bits in (8, 12, 14):
            x = (np.random.default_rng(bits).integers(0, (1 << bits)+1, (620, 5))/(1 << bits)).astype(np.float32)
            names = ['a','b','c','d','e']
            with codec.StreamEncoder(names, 25., bits=bits) as enc:
                header = enc.header
                chunks = [enc.push(frame) for frame in x]+[enc.finish()]
            saved = header+b''.join(chunks)
            self.assertEqual(codec.encode(x, names, 25., bits=bits), saved)
            for fragment in (1, 7, 8192):
                with self.subTest(bits=bits, fragment=fragment):
                    payload = b''.join(chunks)
                    with codec.StreamDecoder(header) as dec:
                        result = [frame for i in range(0, len(payload), fragment)
                                  for frame in dec.feed(payload[i:i + fragment])]
                        self.assertTrue(dec.ended)
                    self.assertTrue(exact(np.asarray(result), x))

    def test_batch_and_stream_share_one_format(self):
        bits = 12
        scale = 1 << bits
        # Exercise the periodic span boundary on a 62-channel stream.
        x = (np.random.default_rng(7).integers(0, scale + 1, (620, 62)) / scale).astype(np.float32)
        names = [f'control_{i}' for i in range(x.shape[1])]
        with codec.StreamEncoder(names, 120., bits=bits) as enc:
            stream = enc.header + b''.join(enc.push(frame) for frame in x) + enc.finish()
        self.assertEqual(stream[:4], b'CACC')
        self.assertEqual(stream, codec.encode(x, names, 120., bits=bits))
        self.assertTrue(exact(codec.decode(stream)[0], x))
        self.assertTrue(exact(reference.decode(stream)[0], x))
        self.assertIs(codec.StreamEncoder, low_memory.StreamEncoder)
        self.assertIs(codec.StreamDecoder, low_memory.StreamDecoder)
        with self.assertRaises(ValueError):
            codec.StreamEncoder([str(i) for i in range(94)], 120.)

    def test_rejected_input_preserves_encoder(self):
        with codec.StreamEncoder(['a','b'], 25.) as enc:
            with self.assertRaises(ValueError):enc.push(np.zeros(3, np.float32))
            with self.assertRaises(ValueError):enc.push(np.array([.1, .5], np.float32))
            enc.push(np.zeros(2, np.float32))
            enc.finish()

    def test_checksums_and_truncation(self):
        x = np.zeros((20, 3), np.float32)
        body = codec.encode(x, ['a','b','c'], 25.)
        for offset in (5, -9, -1):
            damaged = bytearray(body);damaged[offset] ^= 1
            with self.subTest(offset=offset):
                with self.assertRaises(ValueError):codec.decode(damaged)
        for count in (1, 8, len(body)//2):
            with self.assertRaises(ValueError):codec.decode(body[:-count])


if __name__ == '__main__':
    unittest.main()
