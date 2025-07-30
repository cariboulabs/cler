#pragma once
#include <CaribouLite.hpp>
#include "cler.hpp"

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

    SourceCaribouliteBlock(const char* name,
        CaribouLiteRadio::RadioType radio_type,
        float freq_hz,
        float samp_rate_hz,
        bool agc,
        float rx_gain_db = 0.0f
        ) : cler::BlockBase(name) {
            bool freq_valid = false;
            
            if (!detect_cariboulite_board()) {
                throw std::runtime_error("CaribouLite board not detected!");
            }
            
            CaribouLite& cl = CaribouLite::GetInstance(false);
            _radio = cl.GetRadioChannel(radio_type);
            if (!_radio) {
                throw std::runtime_error("Failed to get radio channel for selected radio type");
            }

            std::vector<CaribouLiteFreqRange> ranges = _radio->GetFrequencyRange();
            for (const auto& range : ranges) {
                if (freq_hz > range.fmin() && freq_hz < range.fmax()) {
                    freq_valid = true;
                }
            }
            if (!freq_valid) {
                throw std::invalid_argument("Freqeuncy is out of range for the selected radio type.");
            }

            if (samp_rate_hz > _radio->GetRxSampleRateMax() || samp_rate_hz < _radio->GetRxSampleRateMin()) {
                throw std::invalid_argument(
                    "samp_rate_hz must be between " +
                    std::to_string(_radio->GetRxSampleRateMin()) + " and " +
                    std::to_string(_radio->GetRxSampleRateMax()) + " Hz, but got " +
                    std::to_string(samp_rate_hz)
                    );
            }

            _max_samples_to_read = _radio->GetNativeMtuSample();

            _radio->SetFrequency(freq_hz);
            _radio->SetRxSampleRate(samp_rate_hz);            
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
            auto [ptr, space] = out->write_span();
            if (ptr && space > 0) {
                // Fast path: single ReadSamples call
                size_t to_read = std::min(space, _max_samples_to_read);
                int ret = _radio->ReadSamples(ptr, to_read);
                if (ret > 0) {
                    out->commit_write(ret);
                }
                if (ret < 0) {
                    return cler::Error::ProcedureError;
                }
                return cler::Empty{};
            }
            return cler::Error::NotEnoughSpace;
        }

        private:    
            CaribouLiteRadio* _radio = nullptr;
            size_t _max_samples_to_read;
};
