#include "rm_log.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace utils
{

void RMLOG::Init(
  const std::string & path, const std::string & console_level, const std::string & file_level,
  const std::string & log_level)
{
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::from_str(console_level));

  // 滚动日志文件：5MB × 3 个轮转
  auto file_sink =
    std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path.c_str(), 1024 * 1024 * 5, 3);
  file_sink->set_level(spdlog::level::from_str(file_level));

  spdlog::sinks_init_list sink_list = {file_sink, console_sink};

  spdlog::init_thread_pool(4096, 1);
  logger_sptr_ = std::make_shared<spdlog::async_logger>(
    "multi_sink", sink_list.begin(), sink_list.end(), spdlog::thread_pool(),
    spdlog::async_overflow_policy::block);
  logger_sptr_->set_level(spdlog::level::from_str(log_level));
  // info 及以上级别立刻刷盘，便于实时查看识别结果（否则 info 会积在文件缓冲里）
  logger_sptr_->flush_on(spdlog::level::info);

  // 格式：时间 / 级别 / 线程 / 文件 函数:行号 / 文本
  logger_sptr_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] [%s %!:%#] %v");
}

void RMLOG::SetLevel(const std::string & log_level)
{
  auto level = spdlog::level::from_str(log_level);
  if (level == spdlog::level::n_levels) {
    RM_LOG_WARN("Given invalid log level {}", log_level);
  } else {
    logger_sptr_->set_level(level);
  }
}

}  // namespace utils
