# websdr — the receiver in a browser, over ssh

Build (HackRF support needs libhackrf-dev at configure time):

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j --target websdr
    sudo install build/desktop_examples/websdr/websdr /usr/local/bin/

Run on the box with the SDR plugged in — no arguments needed, it lists what it
finds and auto-opens the only device:

    websdr                       # http://127.0.0.1:8080, Devices list first
    websdr --source sim          # no hardware

From your laptop:

    ssh -L 8080:localhost:8080 box
    # open http://localhost:8080

LAN viewers (token required whenever the bind is not loopback; the first
browser holding the token controls, the rest view):

    websdr --bind 0.0.0.0 --token s3cret      # http://box:8080/?token=s3cret

Service: `sudo cp misc/websdr/cler-websdr.service /etc/systemd/system/`,
`sudo useradd -r -G plugdev websdr`, `sudo systemctl enable --now cler-websdr`.
The unit persists the last device/tuning in /var/lib/cler-websdr/state.json.
`websdr --version` and `GET /health` tell you which build is running.

HackRF as a non-root user needs libhackrf's udev rule (Debian/Ubuntu/Raspberry
Pi OS ship it as `/lib/udev/rules.d/60-libhackrf0.rules` with the `libhackrf0`
package; a source build installs `53-hackrf.rules`); the service user must be
in `plugdev`.

Alternative for a LAN with no browser requirement: run SoapySDRServer on the
box and point the desktop scanner at `driver=remote` — all DSP on the laptop.

Build for a Raspberry Pi from any machine (qemu, no sysroot):

    docker buildx build --platform linux/arm64 --build-arg BASE=debian:bullseye \
      -f docker/Dockerfile.build --target out -o out/linux-arm64 .
    scp out/linux-arm64/websdr pi@box:/usr/local/bin/
