"""No webcam, model, network or external dataset is needed."""
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'src'))
from avatar_codec import encode, decode, StreamEncoder, StreamDecoder


def main():
    # A synthetic quantized control track, not a real avatar performance.
    bits, fps, channels = 12, 25., 54
    t = np.arange(750)[:, None]
    q = np.rint(2048+1200*np.sin(t*.035+np.arange(channels)[None, :]*.13))
    x = np.ascontiguousarray(q/(1 << bits), np.float32)
    names = [f'control_{i}' for i in range(channels)]
    blob = encode(x, names, fps, bits=bits)
    y, _ = decode(blob)
    assert np.array_equal(x.view('u4'), y.view('u4'))
    chunks, output = [], []
    with StreamEncoder(names, fps, bits=bits) as enc:
        header = enc.header
        with StreamDecoder(header) as dec:
            for frame in x:
                chunk = enc.push(frame)
                chunks.append(chunk)
                output.extend(dec.feed(chunk))
            tail = enc.finish()
            output.extend(dec.feed(tail))
            assert dec.ended and dec.seen == len(x)
    assert header+b''.join(chunks)+tail == blob
    assert np.array_equal(np.asarray(output).view('u4'), x.view('u4'))
    rate = len(blob)*8/(len(x)/fps)/1000
    print(f'{len(x)} frames, {channels} channels, {len(blob)} bytes, {rate:.3f} kbps')
    print('Batch and streaming: identical bytes and exact quantized reconstruction.')
    print('This synthetic rate is an example, not a research benchmark result.')


if __name__ == '__main__':
    main()
