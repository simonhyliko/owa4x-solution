#include "mf4_writer.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <vector>
#include <fstream>
#include <cmath>
#include <atomic>
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
    close_current_file();
    
    current_file_path_ = generate_filename();
    current_file_size_ = 0;
    channel_groups_.clear();
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

        // DO NOT start measurement yet - defer until first sample
        // This prevents timestamp resets when frames arrive before StartMeasurement
        measurement_started_ = false;
        
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

void Mf4Writer::write_can_message_internal(const CanMessage& message) {
    if (!mdf_writer_ || !data_group_ || message.signals.empty()) {
        return;
    }
    
    // PROTECTION: Don't write if we're shutting down
    if (shutdown_requested_.load()) {
        static int dropped_count = 0;
        if (++dropped_count <= 5) {
            std::cout << "🛑 Dropping message during shutdown (CAN ID 0x" 
                      << std::hex << message.can_id << std::dec << ")" << std::endl;
        }
        return;
    }

    // Start measurement on first sample to anchor timebase to first frame
    if (!measurement_started_) {
        measurement_start_steady_ = message.timestamp;  // Anchor to first frame timestamp
        measurement_start_system_ = std::chrono::system_clock::now();
        measurement_start_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            measurement_start_system_.time_since_epoch()).count();
        
        mdf_writer_->StartMeasurement(measurement_start_ns_);
        measurement_started_ = true;
        
        std::cout << "🚀 Started MF4 measurement anchored to first CAN frame (ID 0x" 
                  << std::hex << message.can_id << std::dec << ")" << std::endl;
    }
    
    // PROTECTION: Reject messages with timestamps older than our measurement start
    // This can happen during file rotation or if there are buffered old messages
    auto delta = message.timestamp - measurement_start_steady_;
    if (delta < std::chrono::steady_clock::duration::zero()) {
        static int rejected_count = 0;
        if (++rejected_count <= 10) {  // Log only first 10 rejections to avoid spam
            auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
            std::cerr << "🚫 REJECTED old message (CAN ID 0x" << std::hex << message.can_id << std::dec 
                      << ", " << delta_ms << "ms before measurement start)" << std::endl;
        }
        return;  // Skip this message
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

        // Debug: Log suspicious timestamps
        static int message_count = 0;
        message_count++;
        
        // Only flag truly suspicious timestamps (not the first message at 0.0)
        if ((relative_seconds < 0.0 || relative_seconds > 1000000.0) && message_count > 1) {
            std::cerr << "⚠️  SUSPICIOUS TIMESTAMP detected!" << std::endl;
            std::cerr << "   Message #" << message_count << ", CAN ID 0x" << std::hex << message.can_id << std::dec << std::endl;
            std::cerr << "   Relative seconds: " << std::fixed << std::setprecision(9) << relative_seconds << std::endl;
            std::cerr << "   Timestamp NS: " << timestamp_ns << std::endl;
            std::cerr << "   Measurement started: " << (measurement_started_ ? "YES" : "NO") << std::endl;
            if (measurement_started_) {
                auto delta = message.timestamp - measurement_start_steady_;
                auto delta_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count();
                std::cerr << "   Delta from start (ns): " << delta_ns << std::endl;
            }
            std::cerr << "   Signals in message: " << message.signals.size() << std::endl;
        }

        if (cg_info->master_channel) {
            cg_info->master_channel->SetChannelValue(relative_seconds);
        }
        
        // Set values for all channels in this message
        for (const auto& signal : message.signals) {
            auto* channel = get_or_create_channel(cg_info, signal);
            if (channel) {
                double safe_value = signal.value;
                
                // PROTECTION: Sanitize extreme signal values
                if (std::isnan(safe_value) || std::isinf(safe_value)) {
                    std::cerr << "🔧 SANITIZED NaN/Inf signal: " << signal.signal_name 
                              << " (was " << signal.value << ") -> 0.0" << std::endl;
                    safe_value = 0.0;
                } else if (std::abs(safe_value) > 1e12) {
                    std::cerr << "🔧 SANITIZED extreme signal: " << signal.signal_name 
                              << " (was " << signal.value << ") -> clamped" << std::endl;
                    safe_value = (safe_value > 0) ? 1e12 : -1e12;
                }
                
                channel->SetChannelValue(safe_value);
            }
        }
        
        // Save the complete sample to the channel group (all signals at once)
        mdf_writer_->SaveSample(*cg_info->channel_group, timestamp_ns);
        
        // Debug: Log occasionally with signal values
        if (message_count % 100 == 0) {
            std::cout << "Written " << message_count << " CAN messages, last: CAN ID 0x" 
                      << std::hex << message.can_id << std::dec
                      << " with " << message.signals.size() << " signals, time=" 
                      << std::fixed << std::setprecision(3) << relative_seconds << "s" << std::endl;
            
            // Log first few signal values for debugging
            std::cout << "  Signal values: ";
            int signal_count = 0;
            for (const auto& signal : message.signals) {
                if (signal_count++ < 3) {  // Show only first 3 signals
                    std::cout << signal.signal_name << "=" << signal.value << " ";
                }
            }
            if (message.signals.size() > 3) {
                std::cout << "... (+" << (message.signals.size() - 3) << " more)";
            }
            std::cout << std::endl;
        }
        
        // Special logging for the last few messages before stopping
        static bool stopping_logged = false;
        if (message_count > 990 && !stopping_logged) {
            std::cout << "📊 Final messages - Message #" << message_count 
                      << ", time=" << std::fixed << std::setprecision(6) << relative_seconds << "s" << std::endl;
            for (const auto& signal : message.signals) {
                std::cout << "  " << signal.signal_name << " = " << signal.value << std::endl;
            }
            if (message_count > 999) stopping_logged = true;
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

bool Mf4Writer::start() {
    if (mdf_writer_) {
        std::cerr << "MF4 Writer already started" << std::endl;
        return false;
    }

    if (!load_dbc_definitions()) {
        std::cerr << "MF4 Writer cannot start without DBC definitions." << std::endl;
        return false;
    }

    if (!create_new_file()) {
        std::cerr << "MF4 Writer failed to create initial MF4 file." << std::endl;
        return false;
    }

    return true;
}

void Mf4Writer::write_can_message(const CanMessage& message) {
    if (!mdf_writer_) {
        std::cerr << "MF4 Writer backend not available. Dropping message." << std::endl;
        return;
    }

    if (current_file_size_ >= MAX_FILE_SIZE) {
        std::cout << "MF4 file reached max size, rotating..." << std::endl;
        if (!create_new_file()) {
            std::cerr << "Failed to rotate MF4 file. Message dropped." << std::endl;
            return;
        }
    }

    write_can_message_internal(message);
}

void Mf4Writer::stop() {
    // Signal to stop accepting new messages
    shutdown_requested_.store(true);
    
    std::cout << "🛑 MF4 Writer stopping - no more messages will be accepted" << std::endl;
    
    close_current_file();
    std::cout << "MF4 Writer stopped" << std::endl;
}
