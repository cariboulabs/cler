#!/usr/bin/env python3

import numpy as np
import matplotlib.pyplot as plt

SAMPLES_PER_SYMBOL = 4
SYMBOL_DELAY = 3
UP_COLOR = np.array([0, 0.5, 0])
DOWN_COLOR = np.array([0.5, 0, 0])

def plot_sequence(ax, data, label, bits, samples_per_symbol):
    for i in range(0, data.size, samples_per_symbol):
        ax.axvline(i, color='gray', linestyle='--', alpha=0.5)
        symb_data = data[i:i+samples_per_symbol]
        if len(symb_data) < 2:
            continue  # not enough samples to compute diff
        diff = np.diff(symb_data)
        mean_diff = np.mean(diff)
        symbol = np.sign(mean_diff)

        # Robust symbol-to-color mapping
        if symbol > 0:
            color = UP_COLOR
        elif symbol < 0:
            color = DOWN_COLOR
        else:
            color = [0.5, 0.5, 0.5]  # neutral gray for ambiguous transitions

        ax.plot(np.arange(i, i + len(symb_data)), symb_data, color=color, linewidth=2, marker = 'x')

    ax.set_title(f'{label} - {bits}')
    ax.set_ylabel('Phase (radians)')
    ax.set_xlabel('Sample Index')

def main():
    preamble_bits = "01010101010101010101010101010101"
    syncword_bits = "110100111001000110100110"

    ref_preamble_data = np.unwrap(np.angle(np.fromfile("output/reference_preamble.bin", dtype=np.complex64)))
    ref_syncword_data = np.unwrap(np.angle(np.fromfile("output/reference_syncword.bin", dtype=np.complex64)))

    start_cut_off = int(SAMPLES_PER_SYMBOL * (SYMBOL_DELAY - 0.5))
    ref_preamble_data = ref_preamble_data[start_cut_off: start_cut_off + len(preamble_bits) * SAMPLES_PER_SYMBOL]
    ref_syncword_data = ref_syncword_data[start_cut_off: start_cut_off + len(syncword_bits) * SAMPLES_PER_SYMBOL]

    assert(ref_preamble_data.size == len(preamble_bits) * SAMPLES_PER_SYMBOL)
    assert(ref_syncword_data.size == len(syncword_bits) * SAMPLES_PER_SYMBOL)

    fig, axes = plt.subplots(1, 2, sharex=True)
    plot_sequence(axes[0], ref_preamble_data, "Preamble", preamble_bits, SAMPLES_PER_SYMBOL)
    plot_sequence(axes[1], ref_syncword_data, "Syncword", syncword_bits, SAMPLES_PER_SYMBOL)

    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    main()
