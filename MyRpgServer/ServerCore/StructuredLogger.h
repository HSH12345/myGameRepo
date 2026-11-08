#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace RpgLogging
{
enum class Level
{
    TRACE,
    DEBUG,
    INFO,
    WARNING,
    ERROR_LEVEL,
    CRITICAL
};

inline const char* ToString(Level level)
{
    switch (level)
    {
    case Level::TRACE:
        return "Trace";
    case Level::DEBUG:
        return "Debug";
    case Level::INFO:
        return "Info";
    case Level::WARNING:
        return "Warning";
    case Level::ERROR_LEVEL:
        return "Error";
    case Level::CRITICAL:
        return "Critical";
    default:
        return "Info";
    }

}

inline std::string EscapeJson(const std::string& value)
{
    std::ostringstream output;
    for (const unsigned char CHARACTER : value)
    {
        switch (CHARACTER)
        {
        case '\\':
            output << "\\\\";
            break;
        case '"':
            output << "\\\"";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (CHARACTER < 0x20)
            {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(CHARACTER);
            }
            else
            {
                output << CHARACTER;
            }

        }

    }

    return output.str();
}

class Logger
{
public:
    static bool ShouldWrite(Level level)
    {
        static const Level MINIMUM = []
        {
            char* value = nullptr;
            size_t length = 0;
            if (_dupenv_s(&value, &length, "RPG_LOG_LEVEL") != 0 || value == nullptr)
            {
                return Level::INFO;
            }

            std::string text(value);
            std::free(value);
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });

            if (text == "trace")
            {
                return Level::TRACE;
            }

            if (text == "debug")
            {
                return Level::DEBUG;
            }

            if (text == "warning" || text == "warn")
            {
                return Level::WARNING;
            }

            if (text == "error")
            {
                return Level::ERROR_LEVEL;
            }

            if (text == "critical")
            {
                return Level::CRITICAL;
            }

            return Level::INFO;
        }();

        return level >= MINIMUM;
    }

    static void Write(Level level, const char* category, const std::string& message)
    {
        const auto NOW = std::chrono::system_clock::now();
        const auto MILLISECONDS = std::chrono::duration_cast<std::chrono::milliseconds>(NOW.time_since_epoch()) % 1000;
        const std::time_t TIME = std::chrono::system_clock::to_time_t(NOW);
        std::tm utcTime = {};
        gmtime_s(&utcTime, &TIME);

        std::ostringstream timestamp;
        timestamp << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
                  << MILLISECONDS.count() << 'Z';

        std::ostringstream threadId;
        threadId << std::this_thread::get_id();

        std::ostringstream line;
        line << "@rpglog {\"timestamp\":\"" << timestamp.str() << "\",\"level\":\"" << ToString(level)
             << "\",\"category\":\"" << EscapeJson(category == nullptr ? "General" : category) << "\",\"message\":\""
             << EscapeJson(message) << "\",\"thread\":\"" << EscapeJson(threadId.str()) << "\"}";
        GetState().Enqueue(level, line.str());
    }

    static void Shutdown()
    {
        GetState().Shutdown();
    }

private:
    class State
    {
    public:
        State() : worker(&State::Run, this)
        {
        }

        void Enqueue(Level newLevel, std::string line)
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            if (this->stopping)
            {
                return;
            }

            if (this->queue.size() >= MAX_ENTRIES)
            {
                if (newLevel < Level::WARNING)
                {
                    ++this->dropped;
                    return;
                }

                const auto REPLACEABLE = std::find_if(this->queue.begin(), this->queue.end(),
                                                      [newLevel](const Entry& entry)
                                                      {
                                                          return entry.level < newLevel;
                                                      });

                if (REPLACEABLE == this->queue.end())
                {
                    if (newLevel != Level::CRITICAL)
                    {
                        ++this->dropped;
                        return;
                    }

                    // Preserve the newest process-fatal diagnostic even if the queue
                    // already consists entirely of critical entries.
                    this->queue.pop_front();
                }
                else
                {
                    this->queue.erase(REPLACEABLE);
                }

                ++this->dropped;
            }

            this->queue.push_back(Entry{newLevel, std::move(line)});
            this->cv.notify_one();
        }

        void Shutdown()
        {
            {
                std::lock_guard<std::mutex> lock(this->mutex);
                if (this->stopping)
                {
                    return;
                }

                this->stopping = true;
            }

            this->cv.notify_all();
            if (this->worker.joinable())
            {
                this->worker.join();
            }

        }

    private:
        void Run()
        {
            for (;;)
            {
                std::string line;
                std::size_t droppedNow = 0;
                {
                    std::unique_lock<std::mutex> lock(this->mutex);
                    this->cv.wait(lock,
                                  [this]
                                  {
                                      return this->stopping || !this->queue.empty();
                                  });

                    if (this->queue.empty() && this->stopping)
                    {
                        break;
                    }

                    line = std::move(this->queue.front().line);
                    this->queue.pop_front();
                    droppedNow = this->dropped;
                    this->dropped = 0;
                }

                if (droppedNow != 0)
                {
                    std::cout << "@rpglog {\"level\":\"Warning\",\"category\":\"Logger\",\"message\":\"dropped "
                              << droppedNow << " log entries\"}\n";
                }

                std::cout << line << '\n';
            }

            std::cout.flush();
        }

        static constexpr std::size_t MAX_ENTRIES = 8192;

        struct Entry
        {
            Level level;
            std::string line;
        };

        std::mutex mutex;
        std::condition_variable cv;
        std::deque<Entry> queue;
        std::thread worker;
        bool stopping = false;
        std::size_t dropped = 0;
    };

    static State& GetState()
    {
        static State* state = new State();
        return *state;
    }

};

class RateGate final
{
public:
    bool Allow(Level level)
    {
        if (level < Level::WARNING || level == Level::CRITICAL)
        {
            return true;
        }

        const auto NOW =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        auto start = this->windowStartMilliseconds.load(std::memory_order_relaxed);
        if (start == 0 || NOW - start >= 1000)
        {
            if (this->windowStartMilliseconds.compare_exchange_strong(start, NOW, std::memory_order_acq_rel))
            {
                this->count.store(0, std::memory_order_release);
            }

        }

        return this->count.fetch_add(1, std::memory_order_acq_rel) < MAX_ENTRIES_PER_SECOND;
    }

private:
    static constexpr std::uint32_t MAX_ENTRIES_PER_SECOND = 100;
    std::atomic<std::int64_t> windowStartMilliseconds{0};
    std::atomic<std::uint32_t> count{0};
};

}

#define RPG_LOG(level, category, expression)                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        static RpgLogging::RateGate rpgLogRateGate;                                                                    \
        if (RpgLogging::Logger::ShouldWrite(RpgLogging::Level::level) &&                                               \
            rpgLogRateGate.Allow(RpgLogging::Level::level))                                                            \
        {                                                                                                              \
            std::ostringstream rpgLogStream;                                                                           \
            rpgLogStream << expression;                                                                                \
            RpgLogging::Logger::Write(RpgLogging::Level::level, category, rpgLogStream.str());                         \
        }                                                                                                              \
\
    } while (false)

#define RPG_LOG_TRACE(category, expression) RPG_LOG(TRACE, category, expression)
#define RPG_LOG_DEBUG(category, expression) RPG_LOG(DEBUG, category, expression)
#define RPG_LOG_INFO(category, expression) RPG_LOG(INFO, category, expression)
#define RPG_LOG_WARNING(category, expression) RPG_LOG(WARNING, category, expression)
#define RPG_LOG_ERROR(category, expression) RPG_LOG(ERROR_LEVEL, category, expression)
#define RPG_LOG_CRITICAL(category, expression) RPG_LOG(CRITICAL, category, expression)
