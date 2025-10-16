#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include "thread_safe_queue.h"
#include "can_frame.h"

class CanReader {
private:
    std::string interface_name_;
    int socket_fd_;
    std::atomic<bool> running_;
    std::shared_ptr<ThreadSafeQueue<CanFrame>> output_queue_;
    std::unique_ptr<std::thread> reader_thread_;

    bool open_can_socket();
    void close_can_socket();
    void reader_loop();

public:
    explicit CanReader(const std::string& interface = "can1");
    ~CanReader();

    // Non-copyable
    CanReader(const CanReader&) = delete;
    CanReader& operator=(const CanReader&) = delete;

    bool start(std::shared_ptr<ThreadSafeQueue<CanFrame>> queue);
    void stop();
    bool is_running() const { return running_.load(); }
};
