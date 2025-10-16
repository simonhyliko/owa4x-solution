#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include <linux/can.h>

struct CanFrame {
    uint32_t can_id;
    uint8_t can_dlc;
    uint8_t data[8];
    std::chrono::steady_clock::time_point timestamp;

    CanFrame() = default;
    
    CanFrame(const struct can_frame& frame) 
        : can_id(frame.can_id)
        , can_dlc(frame.can_dlc)
        , timestamp(std::chrono::steady_clock::now()) {
        for (int i = 0; i < 8; ++i) {
            data[i] = frame.data[i];
        }
    }
};

struct DecodedSignal {
    uint32_t can_id;
    std::string signal_name;
    double value;
    std::string unit;
    std::chrono::steady_clock::time_point timestamp;
    
    DecodedSignal() = default;
    
    DecodedSignal(uint32_t id, const std::string& name, double val, 
                 const std::string& u, std::chrono::steady_clock::time_point ts)
        : can_id(id), signal_name(name), value(val), unit(u), timestamp(ts) {}
};
