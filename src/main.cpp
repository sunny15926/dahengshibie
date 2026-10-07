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
  if (!camera.StartStream()) return -1;

  // 2. 构造传统识别类
  traditional_armor::TraditionalDetector detector(config_path);

  cv::Mat frame;
  std::vector<traditional_armor::Armor> armors;
  int frame_count = 0;
  auto window_start = steady_clock::now();

  while (true) {
    // 主循环读取相机帧
    if (!camera.Grab(frame)) {
      RM_LOG_WARN("取帧失败，退出主循环");
      break;
    }

    frame_count += 1;

    // 调用识别类得到装甲目标
    armors = detector.detect(frame);

    // 每秒打印一次帧率与识别结果数（避免刷屏）
    double elapsed = duration<double>(steady_clock::now() - window_start).count();
    if (elapsed >= 1.0) {
      RM_LOG_INFO("FPS: {:.2f}, 识别到 {} 个装甲", frame_count / elapsed, armors.size());
      frame_count = 0;
      window_start = steady_clock::now();
    }

    // 绘制灯条与装甲框
    detector.draw(frame, armors);

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
