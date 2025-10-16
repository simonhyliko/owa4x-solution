#include "dbc_decoder.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <dbcppp/Network.h>
#include "mf4_writer.h"

DbcDecoder::DbcDecoder(const std::string& dbc_file) 
    : dbc_file_path_(dbc_file)
    , running_(false) {
}

DbcDecoder::~DbcDecoder() {
    stop();
}

bool DbcDecoder::load_dbc_file() {
    try {
        std::ifstream idbc(dbc_file_path_);
        if (!idbc.is_open()) {
            std::cerr << "Error: Cannot open DBC file: " << dbc_file_path_ << std::endl;
            return false;
        }

        network_ = dbcppp::INetwork::LoadDBCFromIs(idbc);
        if (!network_) {
            std::cerr << "Error: Failed to load DBC file: " << dbc_file_path_ << std::endl;
            return false;
        }

        // Build message lookup map
        message_map_.clear();
        for (const auto& msg : network_->Messages()) {
            message_map_[msg.Id()] = &msg;
        }

        std::cout << "DBC file loaded successfully: " << dbc_file_path_ 
                  << " (" << message_map_.size() << " messages)" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception loading DBC file: " << e.what() << std::endl;
        return false;
    }
}

void DbcDecoder::decode_frame(const CanFrame& frame) {
    auto it = message_map_.find(frame.can_id);
    if (it == message_map_.end()) {
        // Unknown CAN ID, skip 
        return;
    }

    const dbcppp::IMessage* message = it->second;
    
    if (!writer_) {
        return;
    }

    try {
        CanMessage decoded_message;
        decoded_message.can_id = frame.can_id;
        decoded_message.timestamp = frame.timestamp;

        // Debug: Check for timestamp issues at decode time
        static auto first_frame_time = std::chrono::steady_clock::time_point{};
        static bool first_frame_logged = false;
        
        if (!first_frame_logged) {
            first_frame_time = frame.timestamp;
            first_frame_logged = true;
            std::cout << "🔍 First CAN frame decoded at decoder level" << std::endl;
        }
        
        auto time_since_first = frame.timestamp - first_frame_time;
        auto time_since_first_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_first).count();
        
        for (const auto& signal : message->Signals()) {
            double raw_value = signal.RawToPhys(signal.Decode(frame.data));

            // Debug: Log suspicious decoded values
            if (std::abs(raw_value) > 1e12 || std::isnan(raw_value) || std::isinf(raw_value)) {
                std::cerr << "⚠️  SUSPICIOUS DECODED VALUE from DBC: " << signal.Name() 
                          << " = " << raw_value << " (CAN ID 0x" << std::hex << frame.can_id << std::dec 
                          << ", time since first: " << time_since_first_ms << "ms)" << std::endl;
            }

            decoded_message.signals.emplace_back(
                frame.can_id,
                signal.Name(),
                raw_value,
                signal.Unit(),
                frame.timestamp
            );
        }

        writer_->write_can_message(decoded_message);
    } catch (const std::exception& e) {
        std::cerr << "Error decoding CAN frame ID 0x" << std::hex << frame.can_id 
                  << ": " << e.what() << std::dec << std::endl;
    }
}

void DbcDecoder::decoder_loop() {
    std::cout << "DBC Decoder thread started" << std::endl;
    
    CanFrame frame;
    while (running_.load()) {
        if (input_queue_->wait_and_pop(frame, std::chrono::milliseconds(100))) {
            decode_frame(frame);
        }
    }

    // Process remaining frames in queue before stopping
    while (input_queue_->pop(frame)) {
        decode_frame(frame);
    }

    std::cout << "DBC Decoder thread stopped" << std::endl;
}

bool DbcDecoder::start(std::shared_ptr<ThreadSafeQueue<CanFrame>> input_queue,
                       Mf4Writer* writer) {
    if (running_.load()) {
        std::cerr << "DBC Decoder already running" << std::endl;
        return false;
    }

    if (!input_queue || !writer) {
        std::cerr << "Invalid resources provided to DBC Decoder" << std::endl;
        return false;
    }

    if (!load_dbc_file()) {
        return false;
    }

    input_queue_ = input_queue;
    writer_ = writer;

    running_.store(true);
    decoder_thread_ = std::make_unique<std::thread>(&DbcDecoder::decoder_loop, this);
    
    return true;
}

void DbcDecoder::stop() {
    if (running_.load()) {
        running_.store(false);
        
        if (decoder_thread_ && decoder_thread_->joinable()) {
            decoder_thread_->join();
        }
        
        input_queue_.reset();
        decoder_thread_.reset();
        network_.reset();
        message_map_.clear();
        writer_ = nullptr;
        
        std::cout << "DBC Decoder stopped" << std::endl;
    }
}
