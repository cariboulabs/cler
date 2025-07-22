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
        float rx_gain_db = 0.0f
        ) : cler::BlockBase(name) {
            if (!detect_cariboulite_board()) {
                throw std::runtime_error("CaribouLite board not detected!");
            }
            
            CaribouLite& cl = CaribouLite::GetInstance(false);
            _radio = cl.GetRadioChannel(radio_type);
            if (!_radio) {
                throw std::runtime_error("Failed to get radio channel for selected radio type");
            }

            std::vector<CaribouLiteFreqRange> ranges = _radio->GetFrequencyRange();
            bool valid_freq = false;
            for (const auto& range : ranges) {
                if (range.fmin() <= freq_hz && range.fmax() >= freq_hz) {
                    valid_freq = true;
                    break;
                }
            }
            if (!valid_freq) {
                throw std::invalid_argument("Sample rate is out of range for the selected radio type.");
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

        cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<short>>* out) {
            size_t transferable = std::min({out->space(), _max_samples_to_read});
            if (transferable == 0) {
                return cler::Error::NotEnoughSpace;
            }

            size_t sz1, sz2;
            std::complex<short>* ptr1, *ptr2;
            out->peek_write(ptr1, sz1, ptr2, sz2);

            if (sz1 > transferable) {
                sz1 = transferable;
            }
            if (sz2 > transferable - sz1) {
                sz2 = transferable - sz1;
            }

            size_t total_written = 0;
            if (sz1 > 0 && ptr1) {
                int ret = _radio->ReadSamples(ptr1, sz1);
                if (ret < 0) {
                    return cler::Error::ProcedureError; 
                }    
                 total_written += static_cast<size_t>(ret); 
            };

            //we have space, but cariboulabs doesnt have samples to give
            if (static_cast<size_t>(total_written) < sz1) {
                out->commit_write(total_written);
                return cler::Empty{};
            };

            if (sz2 > 0 && ptr2) {
                int ret = _radio->ReadSamples(ptr2, sz2);
                if (ret < 0) {
                    return cler::Error::ProcedureError; 
                }    
                 total_written += static_cast<size_t>(ret); 
            };

            out->commit_write(total_written);
            return cler::Empty{};
        }

        private:    
            CaribouLiteRadio* _radio = nullptr;
            size_t _max_samples_to_read;
};
