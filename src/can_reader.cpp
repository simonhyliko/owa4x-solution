#include "can_reader.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <chrono>

CanReader::CanReader(const std::string& interface) 
    : interface_name_(interface)
    , socket_fd_(-1)
    , running_(false) {
}

CanReader::~CanReader() {
    stop();
}

bool CanReader::open_can_socket() {
    socket_fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socket_fd_ < 0) {
        std::cerr << "Error creating CAN socket: " << strerror(errno) << std::endl;
        return false;
    }

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, interface_name_.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    
    if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
        std::cerr << "Error getting interface index for " << interface_name_ 
                  << ": " << strerror(errno) << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    struct sockaddr_can addr;
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Error binding CAN socket: " << strerror(errno) << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // Set socket to non-blocking mode
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags == -1 || fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Error setting socket to non-blocking: " << strerror(errno) << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    std::cout << "CAN socket opened successfully on " << interface_name_ << std::endl;
    return true;
}

void CanReader::close_can_socket() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void CanReader::reader_loop() {
    struct can_frame frame;
    fd_set read_fds;
    struct timeval timeout;
    
    std::cout << "CAN Reader thread started" << std::endl;

    while (running_.load()) {
        FD_ZERO(&read_fds);
        FD_SET(socket_fd_, &read_fds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms timeout

        int select_result = select(socket_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (select_result > 0 && FD_ISSET(socket_fd_, &read_fds)) {
            ssize_t nbytes = read(socket_fd_, &frame, sizeof(struct can_frame));
            
            if (nbytes == sizeof(struct can_frame)) {
                CanFrame can_frame(frame);
                output_queue_->push(std::move(can_frame));
                
                // Debug: Log occasionally
                static int frame_count = 0;
                if (++frame_count % 500 == 0) {
                    std::cout << "Read " << frame_count << " CAN frames, last ID: 0x" 
                              << std::hex << frame.can_id << std::dec << std::endl;
                }
            } else if (nbytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "CAN read error: " << strerror(errno) << std::endl;
                break;
            }
        } else if (select_result < 0 && errno != EINTR) {
            std::cerr << "CAN select error: " << strerror(errno) << std::endl;
            break;
        }
    }

    std::cout << "CAN Reader thread stopped" << std::endl;
}

bool CanReader::start(std::shared_ptr<ThreadSafeQueue<CanFrame>> queue) {
    if (running_.load()) {
        std::cerr << "CAN Reader already running" << std::endl;
        return false;
    }

    if (!queue) {
        std::cerr << "Invalid output queue provided" << std::endl;
        return false;
    }

    output_queue_ = queue;

    if (!open_can_socket()) {
        return false;
    }

    running_.store(true);
    reader_thread_ = std::make_unique<std::thread>(&CanReader::reader_loop, this);
    
    return true;
}

void CanReader::stop() {
    if (running_.load()) {
        running_.store(false);
        
        if (reader_thread_ && reader_thread_->joinable()) {
            reader_thread_->join();
        }
        
        close_can_socket();
        output_queue_.reset();
        reader_thread_.reset();
        
        std::cout << "CAN Reader stopped" << std::endl;
    }
}