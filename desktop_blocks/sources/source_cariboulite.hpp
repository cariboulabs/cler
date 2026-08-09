#pragma once
#include <CaribouLite.hpp>
#include "cler.hpp"
#include "cler_desktop_utils.hpp"

inline bool detect_cariboulite_board()
{
    CaribouLite::SysVersion ver;
    std::string name;
    std::string guid;

    if (CaribouLite::DetectBoard(&ver, name, guid))
    {
        std::cout << "Detected Version: " << CaribouLite::GetSystemVersionStr(ver) 
                                          << ", Name: " << name 
                                          << ", GUID: " << guid 
                                          << std::endl;
        return true;
    }
    return false;
}

template <typename T>
struct SourceCaribouliteBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, std::complex<short>> || std::is_same_v<T, std::complex<float>>,
            "SourceCaribouliteBlock only supports std::complex<short> or std::complex<float>");
    static constexpr bool may_block = true;

    SourceCaribouliteBlock(const char* name,
        CaribouLiteRadio::RadioType radio_type,
        float freq_hz,
        float samp_rate_hz,
        bool agc,
        float rx_gain_db = 0.0f,
        float bw_hz      = 0.0f
        ) : cler::BlockBase(name) {
            bool freq_valid = false;

            if (!detect_cariboulite_board()) {
                cler::panic("CaribouLite board not detected!");
            }

            CaribouLite& cl = CaribouLite::GetInstance(false);
            _radio = cl.GetRadioChannel(radio_type);
            if (!_radio) {
                cler::panic("Failed to get radio channel for selected radio type");
            }

            std::vector<CaribouLiteFreqRange> ranges = _radio->GetFrequencyRange();
            for (const auto& range : ranges) {
                if (freq_hz > range.fmin() && freq_hz < range.fmax()) {
                    freq_valid = true;
                }
            }
            if (!freq_valid) {
                cler::panic("Frequency is out of range for the selected radio type.");
            }

            if (samp_rate_hz > _radio->GetRxSampleRateMax() || samp_rate_hz < _radio->GetRxSampleRateMin()) {
                char msg[160];
                std::snprintf(msg, sizeof(msg),
                    "samp_rate_hz must be between %f and %f Hz, but got %f",
                    _radio->GetRxSampleRateMin(), _radio->GetRxSampleRateMax(), samp_rate_hz);
                cler::panic(msg);
            }

            _max_samples_to_read = _radio->GetNativeMtuSample();

            if (bw_hz > 0.0f &&
                (bw_hz > _radio->GetRxBandwidthMax() || bw_hz < _radio->GetRxBandwidthMin())) {
                char msg[160];
                std::snprintf(msg, sizeof(msg),
                    "bw_hz must be between %f and %f Hz, but got %f",
                    _radio->GetRxBandwidthMin(), _radio->GetRxBandwidthMax(), bw_hz);
                cler::panic(msg);
            }

            _radio->SetFrequency(freq_hz);
            _radio->SetRxSampleRate(samp_rate_hz);
            if (bw_hz > 0.0f) {
                _radio->SetRxBandwidth(bw_hz);
            }
            _radio->SetAgc(agc);
            if (!agc) {_radio->SetRxGain(rx_gain_db);}

            _radio->StartReceiving();
        }

        ~SourceCaribouliteBlock() {
            if (_radio) {
                _radio->StopReceiving();
            }            
        }

        cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
            auto [ptr, space] = out->write_dbf();
            if (ptr == nullptr || space == 0) {
                return cler::Error::NotEnoughSpace;
            }

            size_t to_read = std::min(space, _max_samples_to_read);
            int ret = _radio->ReadSamples(ptr, to_read);
            if (ret < 0) {
                return cler::Error::ProcedureError;
            }
            if (ret == 0) {
                return cler::Error::NotEnoughSamples;
            }
            out->commit_write(ret);
            return cler::Empty{};
        }

        private:    
            CaribouLiteRadio* _radio = nullptr;
            size_t _max_samples_to_read;
};
