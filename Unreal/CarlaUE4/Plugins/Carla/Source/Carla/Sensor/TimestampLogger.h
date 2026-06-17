#pragma once
#include <fstream>
#include <string>
#include <mutex>

class TimestampLogger {
public:
    static TimestampLogger& GetInstance();

    void Log(const std::string& event, double timestamp, int frame);

private:
    TimestampLogger();
    ~TimestampLogger();
    TimestampLogger(const TimestampLogger&) = delete;
    TimestampLogger& operator=(const TimestampLogger&) = delete;

    std::ofstream log_file_;
    std::mutex log_mutex_;
    bool header_written_;
};