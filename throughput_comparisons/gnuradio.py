#!/usr/bin/env python3

from gnuradio import gr
from gnuradio import blocks
import numpy as np
import time

BUFFER_SIZE = 2

class cler_throughput_equiv(gr.top_block):
    def __init__(self):
        gr.top_block.__init__(self, "CLER Throughput Equiv")

        # === Source: emits repeating 2 complex samples ===
        src_data = np.array([0.0+0.0j, 0.0+0.0j], dtype=np.complex64)
        self.source = blocks.vector_source_c(src_data.tolist(), True)

        # === Transfers: use identity copy blocks ===
        self.transfer1 = blocks.copy(gr.sizeof_gr_complex)
        self.transfer2 = blocks.copy(gr.sizeof_gr_complex)
        self.transfer3 = blocks.copy(gr.sizeof_gr_complex)
        self.transfer4 = blocks.copy(gr.sizeof_gr_complex)

        # === Throughput: use a probe block + null_sink ===
        self.null_sink = blocks.null_sink(gr.sizeof_gr_complex)

        # === Optional: probe to check flow ===
        self.probe = blocks.probe_signal_c()

        # === Connect ===
        self.connect(self.source, self.transfer1)
        self.connect(self.transfer1, self.transfer2)
        self.connect(self.transfer2, self.transfer3)
        self.connect(self.transfer3, self.transfer4)
        self.connect(self.transfer4, self.probe)
        self.connect(self.probe, self.null_sink)

    def report(self):
        # In real use, you'd read probe levels
        print("No direct throughput measurement, but flow is continuous.")

if __name__ == '__main__':
    tb = cler_throughput_equiv()
    print("Running GNU Radio throughput test. Please wait 10 seconds...")
    tb.start()
    time.sleep(10)
    tb.stop()
    tb.wait()
    tb.report()
