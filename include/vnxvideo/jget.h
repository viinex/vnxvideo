#pragma once

#include "json.hpp"

template <typename T>
T jget(const nlohmann::json &value, const std::string &key, T defval = T()) {
    if (value.is_object()) {
        auto it = value.find(key);
        if (it != value.end()) {
            try {
                return it.value().get<T>();
            }
            catch (...) {

            }
        }
    }
    return defval;
}

template <typename T>
bool mjget(const nlohmann::json &value, const std::string &key, T& res) {
    if (value.is_object()) {
        auto it = value.find(key);
        if (it != value.end()) {
            try {
                res=it.value().get<T>();
                return true;
            }
            catch (...) {
                return false;
            }
        }
    }
    return false;
}
