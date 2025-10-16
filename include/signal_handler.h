#pragma once

#include <atomic>
#include <functional>

class SignalHandler {
private:
    static std::atomic<bool> shutdown_requested_;
    static std::function<void()> cleanup_callback_;
    
    static void signal_handler(int signum);

public:
    // Install signal handlers for graceful shutdown
    static void install_handlers();
    
    // Set callback to be called on shutdown signal
    static void set_cleanup_callback(std::function<void()> callback);
    
    // Check if shutdown was requested
    static bool shutdown_requested() { return shutdown_requested_.load(); }
    
    // Request shutdown programmatically
    static void request_shutdown() { shutdown_requested_.store(true); }
};
