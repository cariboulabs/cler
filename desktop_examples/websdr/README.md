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
`sudo cp misc/websdr/cler-websdr.default /etc/default/cler-websdr`,
`sudo useradd -r -G plugdev websdr`, `sudo systemctl enable --now cler-websdr`.
Put `--bind`, `--port`, `--token` and friends in `WEBSDR_ARGS` in
/etc/default/cler-websdr; the unit itself only sets the state file and the
record directory. The unit persists the last device/tuning in
/var/lib/cler-websdr/state.json. `websdr --version` and `GET /health` tell you
which build is running.

The unit is `Type=notify` with `WatchdogSec=30`: websdr pings systemd only while
samples keep arriving, so a process that is alive but wedged is restarted rather
than left serving a frozen waterfall. Outside systemd the pings are a no-op.

Recordings are capped at 20 GB total (`--record-max-bytes`, 0 disables) and
websdr also keeps 200 MB of the filesystem free, deleting whole recordings
oldest-first to stay inside both. The recording in progress is never deleted;
pruning is reported to the browser and counted as `pruned_bytes` in the stats
frame.

Upgrades: install versioned and swap the symlink, so a rollback is one command
and never leaves the box without a binary.

    sudo install build/desktop_examples/websdr/websdr /usr/local/bin/websdr-$(git rev-parse --short HEAD)
    sudo systemctl stop cler-websdr
    sudo ln -sfn /usr/local/bin/websdr-<new> /usr/local/bin/websdr
    sudo systemctl start cler-websdr        # rollback: ln -sfn the old one, restart

A browser tab left open across an upgrade keeps the old JavaScript in memory. The
client files are served `Cache-Control: no-store`, so a reload always fetches the
new ones — reload after upgrading, and if the protocol changed the tab says so
rather than misbehaving.

HackRF as a non-root user needs libhackrf's udev rule (Debian/Ubuntu/Raspberry
Pi OS ship it as `/lib/udev/rules.d/60-libhackrf0.rules` with the `libhackrf0`
package; a source build installs `53-hackrf.rules`); the service user must be
in `plugdev`.

Alternative for a LAN with no browser requirement: run SoapySDRServer on the
box and point the desktop scanner at `driver=remote` — all DSP on the laptop.

Cross-compile in ~2 min with a sysroot (see cmake/toolchains/rpi-aarch64.cmake header;
needs gcc-10-aarch64-linux-gnu on Debian-family hosts to match bullseye):

    cmake -S . -B build-rpi -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rpi-aarch64.cmake \
      -DRPI_SYSROOT=$HOME/sysroots/rpi-bullseye -DCLER_BUILD_BLOCKS_GUI=OFF \
      -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
    cmake --build build-rpi -j --target websdr

Or build for a Raspberry Pi from any machine (qemu, slower, no sysroot):

    docker buildx build --platform linux/arm64 --build-arg BASE=debian:bullseye \
      -f docker/Dockerfile.build --target out -o out/linux-arm64 .
    scp out/linux-arm64/websdr pi@box:/usr/local/bin/
