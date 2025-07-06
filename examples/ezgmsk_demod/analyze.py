#!/usr/bin/env python3

import numpy as np
import matplotlib.pyplot as plt
import os

# --------------------------STREAM DATA-----------------------------------
outputdir = os.path.join(os.path.dirname(__file__), "output")
data = np.fromfile(os.path.join(outputdir,"post_decim_output.bin"), dtype=np.complex64)
preamble_detections = np.fromfile(os.path.join(outputdir,"preamble_detections.bin"), dtype=np.uint32)
syncword_detections = np.fromfile(os.path.join(outputdir,"syncword_detections.bin"), dtype=np.uint32)
header_detections = np.fromfile(os.path.join(outputdir,"header_detections.bin"), dtype=np.uint32)
payload_detections = np.fromfile(os.path.join(outputdir,"payload_detections.bin"), dtype=np.uint32)
phase = np.angle(data)
phase[np.abs(data) < 0.05] = 0


# Plot time domain
fig, axes = plt.subplots(1,1)
ax = axes
ax.plot(phase, label='squelched phase')
for i, detection in enumerate(preamble_detections):
    ax.axvline(x=detection, color='r', linestyle='--',
               label='preamble' if i == 0 else "")
for i, detection in enumerate(syncword_detections):
    ax.axvline(x=detection, color=[0.0,0.8,0.0], linestyle='--',
               label='syncword' if i == 0 else "")
for i, detection in enumerate(header_detections):
    ax.axvline(x=detection, color='k', linestyle='--',
               label='header' if i == 0 else "")
for i, detection in enumerate(payload_detections):
    ax.axvline(x=detection, color=[0.8,0.0,0.8], linestyle='--',
               label='payload' if i == 0 else "")
ax.legend()
ax.set_title('Phase of Samples')
ax.set_ylabel('Phase (radians)')
ax.set_xlabel('Sample Index')

plt.tight_layout()
plt.show()
