# cler as an OpenWebRX source

`openwebrx_connector` speaks the `owrx_connector` protocol, so OpenWebRX can use any
radio cler supports — including CaribouLite, which has no OpenWebRX driver.
OpenWebRX does all the DSP; the connector only delivers full-rate IQ.

    cmake --build build --target openwebrx_connector
    sudo install build/desktop_examples/openwebrx_connector/openwebrx_connector /usr/local/bin/

## With a two-file patch (recommended)

In an OpenWebRX checkout:

    cp misc/openwebrx/cler.py owrx/source/cler.py
    patch -p1 < misc/openwebrx/feature.py.patch

`/var/lib/openwebrx/settings.json`:

    "sdrs": {"cler0": {"name": "cler", "type": "cler", "device": "hackrf",
      "rf_gain": "LNA=32,VGA=20",
      "profiles": {"fm": {"name": "FM", "center_freq": 100000000, "samp_rate": 2400000,
                          "start_freq": 100300000, "start_mod": "wfm"}}}}

`device` takes a cler source id: `hackrf[:serial]`, `pluto:ip:ADDR`, `uhd:ARGS`,
`cariboulite:s1g|hif`, `soapy:ARGS`, `sigmf:NAME`, `sim`, or empty for the first
device found. `rf_gain` is `auto`, a number, or `NAME=V,NAME=V`.

## Without patching OpenWebRX

Install the binary as `sddc_connector` and use `"type": "sddc"` — that source is
a bare connector wrapper, so it only needs the binary and its version string.
The `device` field still reaches the connector even though the UI hides it.

## Licensing

`openwebrx_connector` is a clean-room implementation of the wire protocol and ships
under cler's own license. The files in this directory are derivatives of
OpenWebRX and are AGPL-3.0-or-later; they are not compiled into any cler binary.

## Limits

`-r/--rtltcp` is not implemented: the connector refuses to start rather than
leave OpenWebRX waiting on a dead port, so leave `rtltcp_compat` off. Soapy
`settings` are accepted and ignored. Sample-rate changes restart the flowgraph —
the IQ socket stays open, OpenWebRX keeps reading.
