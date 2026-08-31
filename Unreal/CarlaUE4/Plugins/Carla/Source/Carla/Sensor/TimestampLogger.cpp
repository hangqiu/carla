#include "TimestampLogger.h"

#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <unistd.h>   // gethostname


TimestampLogger::TimestampLogger() {

    // ----------------------------
    // 1. Resolve log path (env override, else default)
    // ----------------------------
    log_path_ = "/tmp/carla_timestamps.csv";

    const char* env_path = std::getenv("CARLA_TIMESTAMP_LOG_PATH");
    if (env_path != nullptr && env_path[0] != '\0') {
        log_path_ = std::string(env_path);
    }

    // ----------------------------
    // 2. Ensure directory exists (once, NOT per line)
    // ----------------------------
    const std::size_t slash = log_path_.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string dir = log_path_.substr(0, slash);
        if (!dir.empty()) {
            const std::string cmd = "mkdir -p " + dir;
            system(cmd.c_str());
        }
    }

    // ----------------------------
    // 3. Cache hostname (once, NOT per line)
    // ----------------------------
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        hostname_ = std::string(hostname);
    } else {
        hostname_ = "unknown";
    }

    // ----------------------------
    // 4. Open file once and keep it open
    // ----------------------------
    log_file_.open(log_path_, std::ios::out | std::ios::app);

    if (!log_file_.is_open()) {
        std::cerr << "[TimestampLogger] Failed to open log file: "
                  << log_path_ << std::endl;
        return;
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

    if (!log_file_.is_open()) {
        return;
    }

    // Write line
    log_file_ << frame << ","
              << std::fixed << timestamp << ","
              << event << ","
              << "server,"
              << hostname_
              << "\n";

    // Flushed per line so a crashed run still yields usable data. This is a
    // single write syscall (~microseconds).
    log_file_.flush();
}
