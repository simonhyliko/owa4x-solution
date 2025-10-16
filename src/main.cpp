#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <getopt.h>
#include <filesystem>

#include "thread_safe_queue.h"
#include "can_frame.h"
#include "can_reader.h"
#include "dbc_decoder.h"
#include "mf4_writer.h"
#include "signal_handler.h"

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --dbc PATH          Path to DBC file (required)\n"
              << "  --output-dir PATH   Output directory for MF4 files (required)\n"
              << "  --interface NAME    CAN interface name (default: can1)\n"
              << "  --help              Show this help message\n"
              << "\nExample:\n"
              << "  " << program_name << " --dbc my_can.dbc --output-dir /tmp/mf4_data\n"
              << std::endl;
}

struct Config {
    std::string dbc_file;
    std::string output_dir;
    std::string can_interface = "can1";
    
    bool is_valid() const {
        return !dbc_file.empty() && !output_dir.empty();
    }
};

Config parse_arguments(int argc, char* argv[]) {
    Config config;
    
    static struct option long_options[] = {
        {"dbc",        required_argument, 0, 'd'},
        {"output-dir", required_argument, 0, 'o'},
        {"interface",  required_argument, 0, 'i'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "d:o:i:h", long_options, &option_index)) != -1) {
        switch (c) {
            case 'd':
                config.dbc_file = optarg;
                break;
            case 'o':
                config.output_dir = optarg;
                break;
            case 'i':
                config.can_interface = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case '?':
                // getopt_long already printed an error message
                exit(1);
            default:
                exit(1);
        }
    }
    
    return config;
}

bool validate_config(const Config& config) {
    if (!config.is_valid()) {
        std::cerr << "Error: Both --dbc and --output-dir are required\n" << std::endl;
        return false;
    }
    
    // Check if DBC file exists
    if (!std::filesystem::exists(config.dbc_file)) {
        std::cerr << "Error: DBC file does not exist: " << config.dbc_file << std::endl;
        return false;
    }
    
    // Create output directory if it doesn't exist
    try {
        std::filesystem::create_directories(config.output_dir);
    } catch (const std::exception& e) {
        std::cerr << "Error: Cannot create output directory: " << e.what() << std::endl;
        return false;
    }
    
    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "=== CAN Socket Collector v" << VERSION << " ===" << std::endl;
    
    // Parse command line arguments
    Config config = parse_arguments(argc, argv);
    
    if (!validate_config(config)) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::cout << "Configuration:\n"
              << "  DBC file: " << config.dbc_file << "\n"
              << "  Output directory: " << config.output_dir << "\n"
              << "  CAN interface: " << config.can_interface << "\n"
              << std::endl;
    
    // Create thread-safe queues
    auto raw_frames_queue = std::make_shared<ThreadSafeQueue<CanFrame>>();
    
    // Create components
    auto can_reader = std::make_unique<CanReader>(config.can_interface);
    auto dbc_decoder = std::make_unique<DbcDecoder>(config.dbc_file);
    auto mf4_writer = std::make_unique<Mf4Writer>(config.output_dir, config.dbc_file);
    
    // Setup cleanup callback for graceful shutdown
    SignalHandler::set_cleanup_callback([&]() {
        std::cout << "Initiating component shutdown..." << std::endl;
        
        // Stop components in reverse order
        if (mf4_writer) mf4_writer->stop();
        if (dbc_decoder) dbc_decoder->stop();
        if (can_reader) can_reader->stop();
    });
    
    // Install signal handlers
    SignalHandler::install_handlers();
    
    // Start components
    std::cout << "Starting components..." << std::endl;

    if (!mf4_writer->start()) {
        std::cerr << "Failed to start MF4 writer" << std::endl;
        return 1;
    }
    
    if (!dbc_decoder->start(raw_frames_queue, mf4_writer.get())) {
        std::cerr << "Failed to start DBC decoder" << std::endl;
        mf4_writer->stop();
        return 1;
    }
    
    if (!can_reader->start(raw_frames_queue)) {
        std::cerr << "Failed to start CAN reader" << std::endl;
        can_reader->stop();
        dbc_decoder->stop();
        mf4_writer->stop();
        return 1;
    }
    
    std::cout << "All components started successfully!" << std::endl;
    std::cout << "CAN Socket Collector is running. Press Ctrl+C to stop." << std::endl;
    
    // Main loop - wait for shutdown signal
    while (!SignalHandler::shutdown_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Check if any component has stopped unexpectedly
        if (!can_reader->is_running() || !dbc_decoder->is_running() || !mf4_writer->is_running()) {
            std::cerr << "One or more components stopped unexpectedly" << std::endl;
            SignalHandler::request_shutdown();
            break;
        }
    }
    
    std::cout << "Stopping components..." << std::endl;
    
    // Stop components in reverse order
    mf4_writer->stop();
    dbc_decoder->stop();
    can_reader->stop();
    
    // Print final statistics
    std::cout << "Final queue sizes:\n"
              << "  Raw frames: " << raw_frames_queue->size() << "\n"
              << std::endl;
    
    std::cout << "CAN Socket Collector stopped gracefully." << std::endl;
    
    return 0;
}
