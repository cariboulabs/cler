#pragma once

#include "cler.hpp"
#include "desktop_blocks/web/web_server.hpp"

#include <algorithm>
#include <array>
#include <cmath>

struct WebSinkBlock : public cler::BlockBase {
    cler::Channel<SpectrumFrame> spectrum;
    cler::Channel<float> audio;

    WebSinkBlock(const char* name, web::WebServer& server, size_t audio_buffer = 1 << 16)
        : cler::BlockBase(name), spectrum(8), audio(audio_buffer), _server(server) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        bool moved = false;
        auto [sptr, ssize] = spectrum.read_dbf();
        for (size_t i = 0; i < ssize; ++i) _server.push_spectrum(sptr[i]);
        if (ssize) { spectrum.commit_read(ssize); moved = true; }

        auto [aptr, asize] = audio.read_dbf();
        size_t done = 0;
        while (done < asize) {
            const size_t n = std::min(asize - done, _pcm.size());
            for (size_t i = 0; i < n; ++i) {
                const float v = std::clamp(aptr[done + i], -1.0f, 1.0f);
                _pcm[i] = static_cast<int16_t>(std::lrint(v * 32767.0f));
            }
            _server.push_audio(_pcm.data(), n);
            done += n;
        }
        if (asize) { audio.commit_read(asize); moved = true; }
        if (!moved) return cler::Error::NotEnoughSamples;
        return cler::Empty{};
    }

private:
    web::WebServer& _server;
    std::array<int16_t, 4096> _pcm{};
};
