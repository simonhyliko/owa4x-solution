#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <unordered_map>
#include "thread_safe_queue.h"
#include "can_frame.h"

namespace dbcppp {
    class INetwork;
    class IMessage;
    class ISignal;
}

class Mf4Writer;

class DbcDecoder {
private:
    std::string dbc_file_path_;
    std::atomic<bool> running_;
    std::shared_ptr<ThreadSafeQueue<CanFrame>> input_queue_;
    std::unique_ptr<std::thread> decoder_thread_;
    Mf4Writer* writer_ = nullptr;
    
    std::unique_ptr<dbcppp::INetwork> network_;
    std::unordered_map<uint32_t, const dbcppp::IMessage*> message_map_;

    bool load_dbc_file();
    void decoder_loop();
    void decode_frame(const CanFrame& frame);

public:
    explicit DbcDecoder(const std::string& dbc_file);
    ~DbcDecoder();

    // Non-copyable
    DbcDecoder(const DbcDecoder&) = delete;
    DbcDecoder& operator=(const DbcDecoder&) = delete;

    bool start(std::shared_ptr<ThreadSafeQueue<CanFrame>> input_queue,
               Mf4Writer* writer);
    void stop();
    bool is_running() const { return running_.load(); }
};
