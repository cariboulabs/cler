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

struct SourceCaribouliteBlock : public cler::BlockBase {
    SourceCaribouliteBlock(const char* name,
        CaribouLiteRadio::RadioType radio_type,
        float freq_hz,
        float samp_rate_hz,
        bool agc,
        float rx_gain_db = 0.0f,
        size_t buffer_size = cler::DEFAULT_BUFFER_SIZE
        ) : cler::BlockBase(name) {
            if (!detect_cariboulite_board()) {
                throw std::runtime_error("CaribouLite board not detected!");
            }
            
            CaribouLite& cl = CaribouLite::GetInstance(false);
            _radio = cl.GetRadioChannel(radio_type);

            std::vector<CaribouLiteFreqRange> ranges = _radio->GetFrequencyRange();
            bool valid_freq = false;
            for (const auto& range : ranges) {
                if (range.fmin() <= freq_hz && range.fmax() >= samp_rate_hz) {
                    valid_freq = true;
                    break;
                }
            }
            if (!valid_freq) {
                throw std::invalid_argument("Sample rate is out of range for the selected radio type.");
            }

            if (samp_rate_hz > 4e6 || samp_rate_hz <= 0) {
                throw std::invalid_argument("samp_rate_hz must be between 1 and 4 Mhz.");
            }

            _radio->SetFrequency(freq_hz);
            _radio->SetRxSampleRate(samp_rate_hz);            
            _radio->SetAgc(agc);
            if (agc) {_radio->SetRxGain(rx_gain_db);}

            _tmp = new std::complex<float>[buffer_size];
            
            _radio->StartReceiving();
        }

        ~SourceCaribouliteBlock() {
            if (_radio) {
                _radio->StopReceiving();
            }
            if (_tmp) {
                delete[] _tmp;
            }
            
        }

        cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
            size_t transferable = out->space();
            int ret = _radio->ReadSamples(_tmp, transferable);
            if (ret < 0) {
                return cler::Error::ProcedureError; 
            }
            out->writeN(_tmp, ret);
            return cler::Empty{};
        }

        private:
        CaribouLiteRadio* _radio = nullptr;
        std::complex<float>* _tmp = nullptr;

};
