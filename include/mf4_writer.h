#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <map>
#include <vector>
#include <filesystem>
#include "thread_safe_queue.h"
#include "can_frame.h"

namespace mdf {
    class MdfWriter;
    class IDataGroup;
    class IChannelGroup;  
    class IChannel;
}

namespace dbcppp {
    class INetwork;
}

// Structure pour regrouper les signaux par message CAN
struct CanMessage {
    uint32_t can_id;
    std::chrono::steady_clock::time_point timestamp;
    std::vector<DecodedSignal> signals;
};

// Structure pour gérer un channel group par message CAN
struct ChannelGroupInfo {
    mdf::IChannelGroup* channel_group = nullptr;
    mdf::IChannel* master_channel = nullptr;
    std::unordered_map<std::string, mdf::IChannel*> channels;
    std::string message_name;
};

struct SignalDefinition {
    std::string name;
    std::string unit;
};

struct MessageDefinition {
    uint32_t can_id = 0;
    std::string name;
    std::vector<SignalDefinition> signals;
};

class Mf4Writer {
private:
    std::string output_directory_;
    std::string dbc_file_path_;
    std::atomic<bool> running_;
    std::shared_ptr<ThreadSafeQueue<DecodedSignal>> input_queue_;
    std::unique_ptr<std::thread> writer_thread_;
    
    std::unique_ptr<mdf::MdfWriter> mdf_writer_;
    std::string current_file_path_;
    size_t current_file_size_;
    static constexpr size_t MAX_FILE_SIZE = 15 * 1024 * 1024; // 15 MB
    
    // Channel management - un channel group par message CAN
    mdf::IDataGroup* data_group_;
    std::unordered_map<uint32_t, ChannelGroupInfo> channel_groups_;
    
    // Buffer pour regrouper les signaux par message et timestamp
    std::map<std::pair<uint32_t, uint64_t>, std::vector<DecodedSignal>> message_buffer_;

    // Gestion du temps de mesure
    std::chrono::steady_clock::time_point measurement_start_steady_;
    std::chrono::system_clock::time_point measurement_start_system_;
    uint64_t measurement_start_ns_;
    bool measurement_started_ = false;
    bool dbc_loaded_ = false;

    std::unique_ptr<const dbcppp::INetwork> dbc_network_;
    std::vector<MessageDefinition> message_definitions_;
    
    bool create_new_file();
    void close_current_file();
    std::string generate_filename();
    void writer_loop();
    void write_signal(const DecodedSignal& signal);
    void write_can_message(const CanMessage& message);
    void flush_message_buffer();
    ChannelGroupInfo* get_or_create_channel_group(uint32_t can_id);
    mdf::IChannel* get_or_create_channel(ChannelGroupInfo* cg_info, const DecodedSignal& signal);
    uint64_t compute_absolute_timestamp(const std::chrono::steady_clock::time_point& timestamp) const;
    double compute_relative_seconds(const std::chrono::steady_clock::time_point& timestamp) const;
    bool load_dbc_definitions();
    bool initialize_channel_groups();

public:
    Mf4Writer(const std::string& output_dir, const std::string& dbc_file);
    ~Mf4Writer();

    // Non-copyable
    Mf4Writer(const Mf4Writer&) = delete;
    Mf4Writer& operator=(const Mf4Writer&) = delete;

    bool start(std::shared_ptr<ThreadSafeQueue<DecodedSignal>> input_queue);
    void stop();
    bool is_running() const { return running_.load(); }
};
