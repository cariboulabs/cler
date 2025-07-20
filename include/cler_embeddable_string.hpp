#pragma once

#include <cstring>
#include <string>
#include <algorithm>

namespace cler {

    template<size_t MaxLen = 64>
    class EmbeddableString {
    public:
        constexpr EmbeddableString() : _data{}, _len(0) {}
        EmbeddableString(const char* str) : _data{}, _len(0) { if (str) append(str); }
        EmbeddableString(const std::string& str) : _data{}, _len(0) { append(str.c_str()); }
        EmbeddableString(const EmbeddableString& other) : _data{}, _len(0) { append(other._data); }

        EmbeddableString& operator=(const EmbeddableString& other) {
            if (this != &other) {
                _len = 0;
                _data[0] = '\0';
                append(other._data);
            }
            return *this;
        }

        EmbeddableString operator+(const char* suffix) const {
            EmbeddableString result(*this);
            result.append(suffix);
            return result;
        }

        EmbeddableString operator+(const EmbeddableString& suffix) const {
            EmbeddableString result(*this);
            result.append(suffix._data);
            return result;
        }

        operator const char*() const { return _data; }
        const char* c_str() const { return _data; }
        size_t length() const { return _len; }
        bool empty() const { return _len == 0; }

    private:
        char _data[MaxLen];
        size_t _len;

        void append(const char* str) {
            if (!str) return;
            size_t str_len = strlen(str);
            size_t copy_len = std::min(str_len, MaxLen - _len - 1);
            memcpy(_data + _len, str, copy_len);
            _len += copy_len;
            _data[_len] = '\0';
        }
    };

} // namespace cler