#pragma once

// 基于 spdlog 封装的日志宏，替换原来代码里的 std::cout / printf 等调试打印。
// 所有调试信息统一走 RM_LOG_* 宏，同时输出到控制台与滚动日志文件。

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace utils
{

#define INIT_LOG(path, console_level, file_level, log_level) \
  utils::RMLOG::instance().Init(path, console_level, file_level, log_level)

#define SET_LOG_LEVEL(log_level) utils::RMLOG::instance().SetLevel(log_level)

#define RM_LOG_BASE(logger, level, ...) \
  (logger)->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, level, __VA_ARGS__)

#define RM_LOG_TRACE(...) RM_LOG_BASE(utils::RMLOG::instance().getLogger(), spdlog::level::trace, __VA_ARGS__)
#define RM_LOG_DEBUG(...) RM_LOG_BASE(utils::RMLOG::instance().getLogger(), spdlog::level::debug, __VA_ARGS__)
#define RM_LOG_INFO(...) RM_LOG_BASE(utils::RMLOG::instance().getLogger(), spdlog::level::info, __VA_ARGS__)
#define RM_LOG_WARN(...) RM_LOG_BASE(utils::RMLOG::instance().getLogger(), spdlog::level::warn, __VA_ARGS__)
#define RM_LOG_ERROR(...) RM_LOG_BASE(utils::RMLOG::instance().getLogger(), spdlog::level::err, __VA_ARGS__)
#define RM_LOG_CRITICAL(...) \
  RM_LOG_BASE(utils::RMLOG::instance().getLogger(), spdlog::level::critical, __VA_ARGS__)

// 日志单例：管理 spdlog logger（控制台 + 文件双 sink）
class RMLOG
{
public:
  static RMLOG & instance()
  {
    static RMLOG rm_log;
    return rm_log;
  }

  std::shared_ptr<spdlog::logger> getLogger() { return logger_sptr_; }

  void Close()
  {
    spdlog::shutdown();
    spdlog::drop_all();
  }

  // 初始化日志：path 为日志文件，console_level/file_level/log_level 分别为控制台/文件/全局级别
  void Init(
    const std::string & path, const std::string & console_level, const std::string & file_level,
    const std::string & log_level);

  void SetLevel(const std::string & log_level);

private:
  RMLOG() = default;

  std::shared_ptr<spdlog::logger> logger_sptr_;
};

}  // namespace utils
