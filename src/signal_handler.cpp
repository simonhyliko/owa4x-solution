#include "signal_handler.h"
#include <csignal>
#include <iostream>
#include <atomic>
#include <functional>

std::atomic<bool> SignalHandler::shutdown_requested_(false);
std::function<void()> SignalHandler::cleanup_callback_;

void SignalHandler::signal_handler(int signum) {
    const char* signal_name = "UNKNOWN";
    
    switch (signum) {
        case SIGINT:
            signal_name = "SIGINT";
            break;
        case SIGTERM:
            signal_name = "SIGTERM";
            break;
        case SIGHUP:
            signal_name = "SIGHUP";
            break;
    }
    
    std::cout << "\nReceived signal " << signal_name << " (" << signum << "), initiating graceful shutdown..." << std::endl;
    
    shutdown_requested_.store(true);
    
    // Call cleanup callback if set
    if (cleanup_callback_) {
        cleanup_callback_();
    }
}

void SignalHandler::install_handlers() {
    // Install signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);   // Ctrl+C
    std::signal(SIGTERM, signal_handler);  // Termination request
    std::signal(SIGHUP, signal_handler);   // Hangup
    
    std::cout << "Signal handlers installed (SIGINT, SIGTERM, SIGHUP)" << std::endl;
}

void SignalHandler::set_cleanup_callback(std::function<void()> callback) {
    cleanup_callback_ = std::move(callback);
}
