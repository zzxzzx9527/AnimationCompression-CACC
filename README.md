# CACC

CACC (Causal Avatar Control Codec) compresses facial and full-body skeletal animation control streams for
digital avatars. It codes bounded residual ranks with a causal probability model and
uses byte-based span termination for incremental decoding. The wire-format
identifier is CACC. Saved streams use the .cacc extension.

## Quick start

The repository has been verified on Windows x64 with CPython 3.10.11,
NumPy 1.26.4 and Zig 0.13.0. Install [Zig](https://ziglang.org/download/) and
replace the example path with the path to your zig.exe.

~~~powershell
py -3.10 -m venv .venv
.\.venv\Scripts\python -m pip install -r requirements.txt
.\.venv\Scripts\python scripts/build_native.py --zig C:\tools\zig\zig.exe
.\.venv\Scripts\python -m pip install -e .
.\.venv\Scripts\python examples/roundtrip.py
.\.venv\Scripts\python -m unittest discover -s tests -v
~~~

The example and tests use synthetic controls and run independently of the
tracking and rendering pipeline.

## API

~~~python
import numpy as np
from avatar_codec import encode, decode, StreamEncoder, StreamDecoder

controls = np.array([[0.0, 0.5], [0.25, 0.75]], dtype=np.float32)
names = ["jaw", "brow"]
encoded = encode(controls, names, 30.0, bits=12)
restored, metadata = decode(encoded)
np.testing.assert_array_equal(restored, controls)

with StreamEncoder(names, 30.0, bits=12) as encoder:
    with StreamDecoder(encoder.header) as decoder:
        parts = [decoded for frame in controls for decoded in decoder.feed(encoder.push(frame))]
        parts.extend(decoder.feed(encoder.finish()))
        assert decoder.ended
        np.testing.assert_array_equal(np.asarray(parts), controls)
~~~

Pass normalized finite float32 controls in [0, 1] on the selected dyadic grid
(k / 2**bits, with 2–14 bits). The codec recovers these quantized values exactly.
Normalize and quantize physical control values before encoding. The paper uses
93 facial controls and 62 skeletal controls.

CACC supports up to 93 control channels. All channels share the adaptive
probability tables across coding spans. Batch and streaming use the same
bitstream. Decode streams from earlier research builds with their original
codec build. StreamDecoder.feed() yields decoded frames. Consume the iterator
before feeding the next chunk. The working_bytes property reports native
decoder state and buffer capacity.

A streaming decoder may emit frames before validating their span CRC32. Batch
decode validates the full stream before returning.

## License

CACC is licensed under the [MIT License](LICENSE). See
[Third-party notices](THIRD_PARTY_NOTICES.md) for build and runtime dependencies.
