#!/usr/bin/env python3
"""Turn a recorded live GDB trace into a standalone offline replay page."""
import json
from pathlib import Path
import sys

path = Path(sys.argv[1]).resolve()
data = json.loads(path.read_text())


def safe_numbers(value):
    # JavaScript Number cannot represent 64-bit virtual addresses exactly.
    if isinstance(value, bool):
        return value
    if isinstance(value, int) and abs(value) >= 1 << 53:
        return hex(value)
    if isinstance(value, dict):
        return {key: safe_numbers(item) for key, item in value.items()}
    if isinstance(value, list):
        return [safe_numbers(item) for item in value]
    return value


template = Path(__file__).with_name('address-demo-template.html').read_text()
payload = json.dumps(safe_numbers(data), ensure_ascii=False).replace('<', '\\u003c')
destination = path.with_name('replay.html')
destination.write_text(template.replace('__LIVE_TRACE_JSON__', payload))
print('Offline replay: ' + str(destination))
