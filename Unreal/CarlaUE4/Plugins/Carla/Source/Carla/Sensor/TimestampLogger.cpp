#include "TimestampLogger.h"

#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <unistd.h>   // gethostname

TimestampLogger::TimestampLogger() : header_written_(false) {

    // ----------------------------
    // 1. Fixed log path
    // ----------------------------
    std::string log_path = "/tmp/carla_timestamps.csv";

    // ----------------------------
    // 2. Ensure directory exists (NO std::filesystem)
    // ----------------------------
    std::string dir = "/tmp";
    std::string cmd = "mkdir -p " + dir;
    system(cmd.c_str());

    // ----------------------------
    // 3. Open file
    // ----------------------------
    log_file_.open(log_path, std::ios::out | std::ios::app);

    if (!log_file_.is_open()) {
        std::cerr << "[TimestampLogger] Failed to open log file: "
                  << log_path << std::endl;
        return;
    }

    // ----------------------------
    // 4. Write header only if file is empty
    // ----------------------------
    log_file_.seekp(0, std::ios::end);
    if (log_file_.tellp() <= 1) {
        log_file_ << "Frame,Timestamp,Event,Agent type,Host\n";
    }

    header_written_ = true;

    // ----------------------------
    // 5. Log startup marker
    // ----------------------------
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        log_file_ << "0,0,LoggerStart,server," << hostname << "\n";
    }
}

TimestampLogger::~TimestampLogger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

TimestampLogger& TimestampLogger::GetInstance() {
    static TimestampLogger instance;
    return instance;
}

void TimestampLogger::Log(
    const std::string& event,
    double timestamp,
    int frame) {

    std::lock_guard<std::mutex> lock(log_mutex_);

    std::string log_path = "/tmp/carla_timestamps.csv";

    const char* env_path = std::getenv("CARLA_TIMESTAMP_LOG_PATH");
    if (env_path != nullptr) {
        log_path = std::string(env_path);
    }

    // Ensure directory exists
    std::string dir = log_path.substr(0, log_path.find_last_of('/'));

    if (!dir.empty()) {
        std::string cmd = "mkdir -p " + dir;
        system(cmd.c_str());
    }

    // Open file fresh every write
    std::ofstream file(log_path, std::ios::app);

    if (!file.is_open()) {
        std::cerr << "[TimestampLogger] Failed to open: "
                  << log_path << std::endl;
        return;
    }

    // Hostname
    char hostname[256] = "unknown";
    gethostname(hostname, sizeof(hostname));

    // Write line
    file << frame << ","
         << std::fixed << timestamp << ","
         << event << ","
         << "server,"
         << hostname
         << "\n";

    file.close();
}