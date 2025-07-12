#pragma once
#include <variant>
#include <string>
#include <cstring>
#include <stdexcept>

namespace cler {

using Empty = std::monostate;

template<typename T, typename E>
class Result {
public:
    Result(T value) : data_(value) {}
    Result(E error) : data_(error) {}

    bool is_ok() const {
        return std::holds_alternative<T>(data_);
    }

    bool is_err() const { 
        return std::holds_alternative<E>(data_);
    }

    T& unwrap() {
        if (!is_ok()) throw std::runtime_error("Called unwrap on an error Result");
        return std::get<T>(data_);
    }

    E& unwrap_err() {
        if (!is_err()) throw std::runtime_error("Called unwrap_err on an ok Result");
        return std::get<E>(data_);
    }

private:
    std::variant<T, E> data_;
};

} //namespace cler