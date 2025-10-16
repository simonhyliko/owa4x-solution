#include "mf4_writer.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <vector>
#include <fstream>
#include <mdf/mdfwriter.h>
#include <mdf/mdffactory.h>
#include <mdf/idatagroup.h>
#include <mdf/ichannelgroup.h>
#include <mdf/ichannel.h>
#include <mdf/cgcomment.h>
#include <mdf/samplerecord.h>
#include <dbcppp/Network.h>

Mf4Writer::Mf4Writer(const std::string& output_dir, const std::string& dbc_file) 
    : output_directory_(output_dir)
    , dbc_file_path_(dbc_file)
    , running_(false)
    , current_file_size_(0)
    , data_group_(nullptr)
    , measurement_start_ns_(0) {
    
    // Create output directory if it doesn't exist
    std::filesystem::create_directories(output_directory_);
}

Mf4Writer::~Mf4Writer() {
    stop();
}

std::string Mf4Writer::generate_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << "can_data_" 
        << std::put_time(&tm, "%Y%m%d_%H%M%S") 
        << ".mf4";
    
    return (std::filesystem::path(output_directory_) / oss.str()).string();
}

bool Mf4Writer::load_dbc_definitions() {
    if (dbc_loaded_) {
        return true;
    }

    if (dbc_file_path_.empty()) {
        std::cerr << "No DBC file provided for MF4 writer. Cannot configure channel layout." << std::endl;
        return false;
    }

    std::ifstream dbc_stream(dbc_file_path_);
    if (!dbc_stream.is_open()) {
        std::cerr << "MF4 writer failed to open DBC file: " << dbc_file_path_ << std::endl;
        return false;
    }

    try {
        dbc_network_ = dbcppp::INetwork::LoadDBCFromIs(dbc_stream);
    } catch (const std::exception& e) {
        std::cerr << "Exception while loading DBC for MF4 writer: " << e.what() << std::endl;
        return false;
    }

    if (!dbc_network_) {
        std::cerr << "Failed to parse DBC file for MF4 writer: " << dbc_file_path_ << std::endl;
        return false;
    }

    message_definitions_.clear();

    for (const auto& message : dbc_network_->Messages()) {
        MessageDefinition definition;
        definition.can_id = message.Id();
        definition.name = message.Name();

        for (const auto& signal : message.Signals()) {
            SignalDefinition sig_def;
            sig_def.name = signal.Name();
            sig_def.unit = signal.Unit();
            definition.signals.emplace_back(std::move(sig_def));
        }

        if (definition.signals.empty()) {
            continue;
        }

        if (definition.name.empty()) {
            std::ostringstream generated;
            generated << "CAN_Message_0x" << std::hex << std::uppercase << definition.can_id;
            definition.name = generated.str();
        }

        message_definitions_.emplace_back(std::move(definition));
    }

    if (message_definitions_.empty()) {
        std::cerr << "DBC file " << dbc_file_path_ << " contains no usable messages for MF4 writer." << std::endl;
        return false;
    }

    dbc_loaded_ = true;
    std::cout << "MF4 writer loaded " << message_definitions_.size()
              << " CAN message definitions from DBC." << std::endl;
    return true;
}

bool Mf4Writer::initialize_channel_groups() {
    if (!data_group_) {
        std::cerr << "Cannot initialize channel groups without a data group." << std::endl;
        return false;
    }

    channel_groups_.clear();

    for (const auto& definition : message_definitions_) {
        auto* channel_group = data_group_->CreateChannelGroup();
        if (!channel_group) {
            std::cerr << "Failed to create channel group for CAN ID 0x"
                      << std::hex << definition.can_id << std::dec << std::endl;
            continue;
        }

        channel_group->Name(definition.name);
        channel_group->RecordId(definition.can_id);

        std::ostringstream comment_stream;
        comment_stream << "CAN message " << definition.name << " (ID 0x"
                       << std::hex << std::uppercase << definition.can_id << std::dec << ")";
        mdf::CgComment comment;
        comment.Comment(comment_stream.str());
        channel_group->SetCgComment(comment);

        auto* master_channel = channel_group->CreateChannel();
        if (!master_channel) {
            std::cerr << "Failed to create master channel for CAN ID 0x"
                      << std::hex << definition.can_id << std::dec << std::endl;
            continue;
        }

        master_channel->Name("timestamp");
        master_channel->DisplayName("Timestamp");
        master_channel->Description("Relative time since start of measurement");
        master_channel->Unit("s");
        master_channel->Type(mdf::ChannelType::Master);
        master_channel->Sync(mdf::ChannelSyncType::Time);
        master_channel->DataType(mdf::ChannelDataType::FloatLe);
        master_channel->DataBytes(sizeof(double));
        master_channel->Decimals(9);

        ChannelGroupInfo cg_info;
        cg_info.channel_group = channel_group;
        cg_info.master_channel = master_channel;
        cg_info.message_name = definition.name;

        for (const auto& signal_def : definition.signals) {
            auto* channel = channel_group->CreateChannel();
            if (!channel) {
                std::cerr << "Failed to create channel " << signal_def.name
                          << " for CAN ID 0x" << std::hex << definition.can_id << std::dec << std::endl;
                continue;
            }

            channel->Name(signal_def.name);
            if (!signal_def.unit.empty()) {
                channel->Unit(signal_def.unit);
            }
            channel->DataType(mdf::ChannelDataType::FloatLe);
            channel->DataBytes(sizeof(double));

            std::ostringstream channel_comment;
            channel_comment << "Signal " << signal_def.name << " from CAN ID 0x"
                            << std::hex << definition.can_id << std::dec;
            if (!signal_def.unit.empty()) {
                channel_comment << " [" << signal_def.unit << "]";
            }
            channel->Description(channel_comment.str());

            cg_info.channels.emplace(signal_def.name, channel);
        }

        channel_groups_.emplace(definition.can_id, std::move(cg_info));
        std::cout << "Configured channel group: " << definition.name
                  << " with " << definition.signals.size() << " signals." << std::endl;
    }

    if (channel_groups_.empty()) {
        std::cerr << "No channel groups configured for MF4 writer." << std::endl;
        return false;
    }

    return true;
}

bool Mf4Writer::create_new_file() {
    // Flush any buffered messages before rolling the file to avoid losing data
    flush_message_buffer();

    close_current_file();
    
    current_file_path_ = generate_filename();
    current_file_size_ = 0;
    channel_groups_.clear();
    message_buffer_.clear();
    buffered_signal_count_ = 0;
    measurement_started_ = false;
    measurement_start_ns_ = 0;
    measurement_start_system_ = std::chrono::system_clock::time_point{};
    measurement_start_steady_ = std::chrono::steady_clock::time_point{};
    
    try {
        mdf_writer_ = mdf::MdfFactory::CreateMdfWriter(mdf::MdfWriterType::Mdf4Basic);
        mdf_writer_->Init(current_file_path_);
        
        // Create data group - les channel groups seront créés à la demande
        data_group_ = mdf_writer_->CreateDataGroup();
        if (!data_group_) {
            std::cerr << "Failed to create data group" << std::endl;
            return false;
        }
        
        if (!load_dbc_definitions()) {
            return false;
        }

        if (!initialize_channel_groups()) {
            return false;
        }

        // Initialize measurement after channel configuration
        mdf_writer_->InitMeasurement();

        // Capture measurement start references and start measurement immediately
        measurement_start_steady_ = std::chrono::steady_clock::now();
        measurement_start_system_ = std::chrono::system_clock::now();
        measurement_start_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            measurement_start_system_.time_since_epoch()).count();
        mdf_writer_->StartMeasurement(measurement_start_ns_);
        measurement_started_ = true;
        
        std::cout << "Created new MF4 file: " << current_file_path_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error creating MF4 file: " << e.what() << std::endl;
        return false;
    }
}

void Mf4Writer::close_current_file() {
    if (mdf_writer_) {
        try {
            // Stop measurement first, then finalize
            if (measurement_started_) {
                const auto stop_system = std::chrono::system_clock::now();
                const auto stop_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    stop_system.time_since_epoch()).count();
                mdf_writer_->StopMeasurement(stop_ns);
            }
            mdf_writer_->FinalizeMeasurement();
            
            // Force flush to disk before reset
            std::cout << "Finalizing MF4 file to disk..." << std::endl;
            mdf_writer_.reset();
            
            if (!current_file_path_.empty()) {
                std::cout << "Closed MF4 file: " << current_file_path_ 
              << " (size: " << current_file_size_ << " bytes)" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error closing MF4 file: " << e.what() << std::endl;
        }
    }
    
    data_group_ = nullptr;
    channel_groups_.clear();
    message_buffer_.clear();
    buffered_signal_count_ = 0;
    measurement_started_ = false;
    measurement_start_ns_ = 0;
    measurement_start_system_ = std::chrono::system_clock::time_point{};
    measurement_start_steady_ = std::chrono::steady_clock::time_point{};
}

ChannelGroupInfo* Mf4Writer::get_or_create_channel_group(uint32_t can_id) {
    auto it = channel_groups_.find(can_id);
    if (it != channel_groups_.end()) {
        return &it->second;
    }
    
    std::cerr << "No channel group configured for CAN ID 0x"
              << std::hex << can_id << std::dec << std::endl;
    return nullptr;
}

mdf::IChannel* Mf4Writer::get_or_create_channel(ChannelGroupInfo* cg_info, const DecodedSignal& signal) {
    if (!cg_info || !cg_info->channel_group) {
        return nullptr;
    }
    
    auto it = cg_info->channels.find(signal.signal_name);
    if (it != cg_info->channels.end()) {
        return it->second;
    }
    
    std::cerr << "Signal " << signal.signal_name
              << " not configured in channel group " << cg_info->message_name << std::endl;
    return nullptr;
}

void Mf4Writer::write_signal(const DecodedSignal& signal) {
    if (!mdf_writer_ || !data_group_) {
        return;
    }
    
    // Convert timestamp to nanoseconds to preserve per-frame precision
    const auto ns_since_epoch = signal.timestamp.time_since_epoch();
    const uint64_t timestamp_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(ns_since_epoch).count();
    
    // Group signals by CAN ID and exact timestamp
    std::pair<uint32_t, uint64_t> message_key = {signal.can_id, timestamp_ns};
    message_buffer_[message_key].push_back(signal);
    
    // Flush periodically to keep latency bounded
    if (++buffered_signal_count_ >= 50) { // flush toutes 50 entrées
        flush_message_buffer();
    }
}

void Mf4Writer::write_can_message(const CanMessage& message) {
    if (!mdf_writer_ || !data_group_ || message.signals.empty()) {
        return;
    }

    if (!measurement_started_) {
        std::cerr << "Attempted to write CAN message before measurement start. Skipping sample." << std::endl;
        return;
    }
    
    // Obtenir ou créer le channel group pour ce message CAN
    auto* cg_info = get_or_create_channel_group(message.can_id);
    if (!cg_info) {
        return;
    }
    
    try {
        // Calculer les horodatages relatif et absolu
        const uint64_t timestamp_ns = compute_absolute_timestamp(message.timestamp);
        const double relative_seconds = compute_relative_seconds(message.timestamp);

        if (cg_info->master_channel) {
            cg_info->master_channel->SetChannelValue(relative_seconds);
        }
        
        // Set values for all channels in this message
        for (const auto& signal : message.signals) {
            auto* channel = get_or_create_channel(cg_info, signal);
            if (channel) {
                channel->SetChannelValue(signal.value);
            }
        }
        
        // Save the complete sample to the channel group (all signals at once)
        mdf_writer_->SaveSample(*cg_info->channel_group, timestamp_ns);
        
        // Debug: Log occasionally
        static int message_count = 0;
        if (++message_count % 20 == 0) {
            std::cout << "Written " << message_count << " CAN messages, last: CAN ID 0x" 
                      << std::hex << message.can_id << std::dec
                      << " with " << message.signals.size() << " signals" << std::endl;
        }
        
        // Update file size estimation
        current_file_size_ += (message.signals.size() + 1) * sizeof(double) + sizeof(uint64_t) + 64;
        
    } catch (const std::exception& e) {
        std::cerr << "Error writing CAN message to MF4: " << e.what() << std::endl;
    }
}

uint64_t Mf4Writer::compute_absolute_timestamp(const std::chrono::steady_clock::time_point& timestamp) const {
    if (!measurement_started_) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    auto delta = timestamp - measurement_start_steady_;
    if (delta < std::chrono::steady_clock::duration::zero()) {
        return measurement_start_ns_;
    }

    const auto delta_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delta);
    return measurement_start_ns_ + static_cast<uint64_t>(delta_ns.count());
}

double Mf4Writer::compute_relative_seconds(const std::chrono::steady_clock::time_point& timestamp) const {
    if (!measurement_started_) {
        return 0.0;
    }

    auto delta = timestamp - measurement_start_steady_;
    if (delta < std::chrono::steady_clock::duration::zero()) {
        return 0.0;
    }

    const auto delta_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delta);
    return static_cast<double>(delta_ns.count()) / 1'000'000'000.0;
}

void Mf4Writer::flush_message_buffer() {
    for (const auto& entry : message_buffer_) {
        const auto& message_key = entry.first;
        const auto& signals = entry.second;
        
        if (!signals.empty()) {
            CanMessage message;
            message.can_id = message_key.first;
            message.timestamp = signals[0].timestamp; // Tous les signaux ont le même timestamp
            message.signals = signals;
            
            write_can_message(message);
        }
    }
    
    message_buffer_.clear();
    buffered_signal_count_ = 0;
}

void Mf4Writer::writer_loop() {
    std::cout << "MF4 Writer thread started" << std::endl;
    
    if (!create_new_file()) {
        std::cerr << "Failed to create initial MF4 file" << std::endl;
        return;
    }
    
    DecodedSignal signal;
    auto last_flush = std::chrono::steady_clock::now();
    
    while (running_.load()) {
        if (input_queue_->wait_and_pop(signal, std::chrono::milliseconds(100))) {
            // Check if we need to rotate the file
            if (current_file_size_ >= MAX_FILE_SIZE) {
                flush_message_buffer();
                if (!create_new_file()) {
                    std::cerr << "Failed to create new MF4 file, stopping writer" << std::endl;
                    break;
                }
            }
            
            write_signal(signal);
            
            // Periodic flush (every 5 seconds)
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_flush).count() >= 5) {
                // Force flush of pending data to disk
                std::cout << "Flushing MF4 data to disk..." << std::endl;
                flush_message_buffer();
                last_flush = now;
            }
        }
    }
    
    // Flush remaining buffered messages
    flush_message_buffer();
    
    // Process remaining signals in queue before stopping
    int remaining = 0;
    while (input_queue_->pop(signal)) {
        write_signal(signal);
        remaining++;
    }
    
    // Final flush
    flush_message_buffer();
    
    if (remaining > 0) {
        std::cout << "Processed " << remaining << " remaining signals before shutdown" << std::endl;
    }
    
    close_current_file();
    std::cout << "MF4 Writer thread stopped" << std::endl;
}

bool Mf4Writer::start(std::shared_ptr<ThreadSafeQueue<DecodedSignal>> input_queue) {
    if (running_.load()) {
        std::cerr << "MF4 Writer already running" << std::endl;
        return false;
    }
    
    if (!input_queue) {
        std::cerr << "Invalid input queue provided to MF4 Writer" << std::endl;
        return false;
    }

    if (!load_dbc_definitions()) {
        std::cerr << "MF4 Writer cannot start without DBC definitions." << std::endl;
        return false;
    }
    
    input_queue_ = input_queue;
    
    running_.store(true);
    writer_thread_ = std::make_unique<std::thread>(&Mf4Writer::writer_loop, this);
    
    return true;
}

void Mf4Writer::stop() {
    if (running_.load()) {
        running_.store(false);
        
        if (writer_thread_ && writer_thread_->joinable()) {
            writer_thread_->join();
        }
        
        close_current_file();
        input_queue_.reset();
        writer_thread_.reset();
        
        std::cout << "MF4 Writer stopped" << std::endl;
    }
}
