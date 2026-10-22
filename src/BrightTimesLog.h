// Bright Times - optional diagnostic log.
//
// Off unless the environment variable BRIGHT_TIMES_LOG is set when the host
// starts:
//   BRIGHT_TIMES_LOG=1            -> %TEMP%\BrightTimes.log (Windows), /tmp/BrightTimes.log
//   BRIGHT_TIMES_LOG=C:\path.log  -> that file
// Every line is flushed immediately, so after a host crash the last lines show
// where it happened.

#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace btlog {

class Log {
public:
    static Log& Get()
    {
        static Log log;
        return log;
    }

    bool enabled() const { return file_ != nullptr; }

    void Write(const char* fmt, va_list args)
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (!file_) return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start_).count();
        const size_t tid = std::hash<std::thread::id>()(std::this_thread::get_id()) % 100000;
        std::fprintf(file_, "%9lld [%05u] ", static_cast<long long>(ms), static_cast<unsigned>(tid));
        std::vfprintf(file_, fmt, args);
        std::fputc('\n', file_);
        std::fflush(file_);
    }

private:
    Log() : start_(std::chrono::steady_clock::now())
    {
        const std::string value = Env("BRIGHT_TIMES_LOG");
        if (value.empty() || value == "0") return;
        std::string path = value;
        if (value == "1") {
#ifdef _WIN32
            std::string dir = Env("TEMP");
            if (dir.empty()) dir = ".";
            path = dir + "\\BrightTimes.log";
#else
            path = "/tmp/BrightTimes.log";
#endif
        }
#ifdef _MSC_VER
        if (fopen_s(&file_, path.c_str(), "a") != 0) file_ = nullptr;
#else
        file_ = std::fopen(path.c_str(), "a");
#endif
        if (file_) std::fprintf(file_, "---- Bright Times log opened ----\n");
    }
    ~Log()
    {
        if (file_) std::fclose(file_);
    }

    static std::string Env(const char* name)
    {
#ifdef _MSC_VER
        char*  buf = nullptr;
        size_t len = 0;
        std::string v;
        if (_dupenv_s(&buf, &len, name) == 0 && buf) v = buf;
        std::free(buf);
        return v;
#else
        const char* v = std::getenv(name);
        return v ? v : "";
#endif
    }

    std::FILE*                            file_ = nullptr;
    std::mutex                            lock_;
    std::chrono::steady_clock::time_point start_;
};

inline bool Enabled() { return Log::Get().enabled(); }

inline void Write(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Log::Get().Write(fmt, args);
    va_end(args);
}

}  // namespace btlog

#define BT_LOG(...)                                       \
    do {                                                  \
        if (btlog::Enabled()) btlog::Write(__VA_ARGS__);  \
    } while (0)
