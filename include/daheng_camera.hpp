#pragma once

// 大恒（Galaxy）工业相机封装类
// 职责：枚举设备 / 按序列号打开 / 配置参数 / 取流转 cv::Mat / 调参 / 释放资源
//
// 典型调用流程：
//   DahengCamera cam;
//   cam.InitLib();              // 1. 全局库初始化（必须最先）
//   cam.EnumerateDevices();     // 2. 枚举，拿到序列号（可选）
//   cam.Open("<序列号>");        // 3. 打开并配置相机
//   cam.StartStream();          // 4. 开始取流
//   cam.Grab(mat);              // 5. 循环取帧（内部已做 Bayer -> BGR）
//   cam.Close();                // 6. 停止取流 + 关设备 + 关库

#include <opencv2/opencv.hpp>
#include <sdk/GxIAPI.h>       // 大恒 Galaxy SDK 主接口（头文件内置在 include/sdk/）
#include <sdk/DxImageProc.h>  // 图像处理接口：DxRaw8toRGB24Ex 等

#include <cstdint>
#include <string>
#include <vector>

namespace traditional_armor
{

// 设备基础信息（序列号 + 型号）
struct DeviceInfo
{
  std::string serial;
  std::string model;
};

class DahengCamera
{
public:
  DahengCamera();
  ~DahengCamera();

  // ---- 生命周期 ----
  bool InitLib();                                        // 全局库初始化，与 Close() 配对
  std::vector<DeviceInfo> EnumerateDevices();            // 枚举相机，返回 (序列号, 型号)
  bool Open(const std::string & serial);                 // 按序列号打开并配置相机
  bool StartStream();                                    // 开始取流
  bool Grab(cv::Mat & image, int timeout_ms = 1000);     // 取一帧（Bayer -> BGR）
  void StopStream();                                     // 停止取流（释放转换缓冲）
  void Close();                                          // 关闭相机 + 关库

  // ---- 属性 ----
  int Width() const { return width_; }
  int Height() const { return height_; }

  // ---- 运行时调参 ----
  bool AddExposureTime(double delta_us);  // 曝光，单位 us
  bool AddGain(double delta_db);          // 增益，单位 dB
  bool AddGamma(double delta);            // 伽马
  // [优化] 绝对设置固定曝光/增益：灯条高亮，短曝光即可，长曝光会锁死相机帧率
  bool SetExposure(double us);  // 固定曝光，单位 us
  bool SetGain(double db);      // 固定增益，单位 dB

private:
  GX_DEV_HANDLE device_ = nullptr;        // 设备句柄
  GX_DS_HANDLE stream_handle_ = nullptr;  // 数据流句柄
  int width_ = 0;
  int height_ = 0;
  int64_t color_filter_ = GX_COLOR_FILTER_NONE;  // Bayer 滤镜排列（RG/GB/GR/BG）
  // [优化] 双缓冲：交替使用两块 RGB 输出缓冲，返回帧在下次取帧前有效，避免每帧 clone
  unsigned char * rgb_buffer_[2] = {nullptr, nullptr};
  int rgb_buffer_index_ = 0;                      // 当前写入缓冲下标

  bool lib_initialized_ = false;
  bool device_opened_ = false;
  bool streaming_ = false;
};

}  // namespace traditional_armor
