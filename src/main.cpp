// 大恒工业相机 + 传统 HSV 装甲识别
//
// 用法：
//   ./dahengshibie [config/armor.yaml]
//
// 流程：
//   循环取流 -> 传统识别（HSV -> 形态学 -> 轮廓 -> 灯条配对）-> 绘制 -> 显示
//   所有调试信息走 RM_LOG（不使用 std::cout）。
//
// 按键：
//   q  退出取流循环，停止采集并释放相机资源

#include <opencv2/opencv.hpp>
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <string>
#include <vector>

#include "daheng_camera.hpp"
#include "rm_log.hpp"
#include "traditional_detector.hpp"

using namespace std::chrono;

int main(int argc, char * argv[])
{
  INIT_LOG("dahengshibie.log", "info", "info", "info");

  // 配置文件路径：默认 config/armor.yaml
  std::string config_path = "config/armor.yaml";
  if (argc > 1) config_path = argv[1];

  auto yaml = YAML::LoadFile(config_path);
  auto serial = yaml["serial"].as<std::string>();
  // 控制台 FPS 打印开关：调试时 true，正式运行 false，避免日志拖慢性能
  auto print_fps = yaml["print_fps"].as<bool>(true);
  // [优化] 固定曝光/增益：短曝光解除相机出帧率上限（需按现场光照标定）
  auto exposure_time = yaml["exposure_time"].as<double>(2000.0);
  auto gain = yaml["gain"].as<double>(0.0);

  // 1. 打开大恒相机（serial 留空则自动枚举取第一台）
  traditional_armor::DahengCamera camera;
  if (!camera.InitLib()) return -1;

  if (serial.empty()) {
    auto devices = camera.EnumerateDevices();
    if (devices.empty()) return -1;
    serial = devices[0].serial;
    RM_LOG_INFO("未指定序列号，自动选择第一台相机: {}", serial);
  }

  if (!camera.Open(serial)) return -1;
  // [优化] 设定固定曝光与增益（灯条高亮，短曝光即可，长曝光会锁死相机帧率）
  camera.SetExposure(exposure_time);
  camera.SetGain(gain);
  if (!camera.StartStream()) return -1;

  // 2. 构造传统识别类
  traditional_armor::TraditionalDetector detector(config_path);

  cv::Mat frame;
  std::vector<traditional_armor::Armor> armors;

  // 帧率统计：用 chrono 计算每帧识别耗时，EMA 平滑后得到 FPS
  double fps_smoothed = 0.0;  // 平滑后的识别帧率
  double process_ms = 0.0;    // 单帧识别耗时（毫秒）
  int frame_count = 0;
  auto window_start = steady_clock::now();

  while (true) {
    // 主循环读取相机帧
    if (!camera.Grab(frame)) {
      RM_LOG_WARN("取帧失败，退出主循环");
      break;
    }

    frame_count += 1;

    // 计时：每帧识别处理耗时（detect 开始到结束）
    auto t0 = steady_clock::now();
    armors = detector.detect(frame);
    auto t1 = steady_clock::now();
    process_ms = duration<double, std::milli>(t1 - t0).count();
    double instant_fps = process_ms > 0.0 ? 1000.0 / process_ms : 0.0;
    // EMA 平滑，避免帧率数字跳变
    fps_smoothed = fps_smoothed <= 0.0 ? instant_fps : 0.9 * fps_smoothed + 0.1 * instant_fps;

    // 控制台打印帧率：每秒一次，受 print_fps 开关控制（正式运行关闭，避免日志拖慢性能）
    if (print_fps) {
      double elapsed = duration<double>(steady_clock::now() - window_start).count();
      if (elapsed >= 1.0) {
        RM_LOG_INFO(
          "FPS: {:.1f}, 识别耗时 {:.2f} ms, 识别到 {} 个装甲", fps_smoothed, process_ms,
          armors.size());
        frame_count = 0;
        window_start = steady_clock::now();
      }
    }

    // 绘制灯条与装甲框
    detector.draw(frame, armors);

    // 实时在画面上显示识别帧率与单帧耗时
    cv::putText(
      frame, cv::format("FPS: %.1f  %.2f ms", fps_smoothed, process_ms), cv::Point(10, 60),
      cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
    cv::putText(
      frame, "q: quit", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
      cv::Scalar(0, 255, 255), 2);
    cv::imshow("dahengshibie", frame);

    if (cv::waitKey(1) == 'q') break;
  }

  camera.Close();
  RM_LOG_INFO("程序退出");
  return 0;
}
