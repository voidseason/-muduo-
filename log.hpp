#pragma once

#include <iostream>
#include <string>
#include <ctime>
#include <sys/time.h>
#include <pthread.h>
#include <mutex>
#include <iomanip>

namespace ns_util
{
    class TimeUtil
    {
    public:
        // 获取秒级时间戳
        static std::string GetTimeStamp()
        {
            struct timeval tv;
            gettimeofday(&tv, nullptr);
            return std::to_string(tv.tv_sec);
        }

        // 获取格式化时间：年-月-日 时:分:秒
        static std::string GetFormatTime()
        {
            struct timeval tv;
            gettimeofday(&tv, nullptr);
            time_t second = tv.tv_sec;

            struct tm* local_time = localtime(&second);
            char buf[128];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                local_time->tm_year + 1900,
                local_time->tm_mon + 1,
                local_time->tm_mday,
                local_time->tm_hour,
                local_time->tm_min,
                local_time->tm_sec);

            return buf;
        }
    };
}

namespace ns_log
{
    enum
    {
        INFO,
        DEBUG,
        WARNING,
        ERROR,
        FATAL
    };

    static std::mutex log_mutex;

    inline std::ostream& Log(const std::string& level, const std::string& file, int line)
    {
        std::lock_guard<std::mutex> lock(log_mutex);

        std::cout << "[" << level << "]"
                  << "[" << ns_util::TimeUtil::GetFormatTime() << "]"
                  << "[0x" << std::hex << pthread_self() << std::dec << "]"
                  << "[" << file << ": " << line << "] ";
        return std::cout;
    }

    #define LOG(level) ns_log::Log(#level, __FILE__, __LINE__)
}
#define DBG_LOG(...) LOG(DEBUG) << __VA_ARGS__