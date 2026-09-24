#!/usr/bin/env python3
"""Print a Settings.ini with some values replaced.
Usage: settings.py <Settings.ini> Key=Value ..."""
import re, sys
text = open(sys.argv[1]).read()
for pair in sys.argv[2:]:
    key, value = pair.split('=', 1)
    text, count = re.subn(rf'^(\t{key} = ).*$', lambda m: m.group(1) + value, text, flags=re.M)
    assert count == 1, f'{key} not found'
sys.stdout.write(text)
