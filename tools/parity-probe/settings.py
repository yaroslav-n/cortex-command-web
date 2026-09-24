#!/usr/bin/env python3
"""Print a Settings.ini that launches straight into <activity> on <scene> with a
probe script enabled.
Usage: settings.py <base Settings.ini> <activity> <scene> [--no-vsync]
The environment can choose the script (PROBE_SCRIPT, default "Parity Probe") and
override further settings (PROBE_SETTINGS="Key=Value Key=Value")."""
import os, re, sys
base, activity, scene = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(base).read()
values = [('LaunchIntoActivity', '1'), ('DefaultActivityType', 'GAScripted'),
          ('DefaultActivityName', activity), ('DefaultSceneName', scene), ('SkipIntro', '1')]
if '--no-vsync' in sys.argv:
    # The native game waits on the display's refresh callback when VSync is on,
    # which never arrives with no display awake.
    values.append(('EnableVSync', '0'))
for pair in os.environ.get('PROBE_SETTINGS', '').split():
    key, value = pair.split('=', 1)
    values.append((key, value))
for key, value in values:
    text, count = re.subn(rf'^(\t{key} = ).*$', lambda m: m.group(1) + value, text, flags=re.M)
    assert count == 1, f'{key} not found in {base}'
script = os.environ.get('PROBE_SCRIPT', 'Parity Probe')
text = text.rstrip('\n') + f'\n\tEnableGlobalScript = ParityProbe.rte/{script}\n'
sys.stdout.write(text)
