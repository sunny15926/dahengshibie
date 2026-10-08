#include "daheng_camera.hpp"

#include "rm_log.hpp"

#include <cstdlib>
#include <cstring>

namespace
{

// 大恒错误码 -> 人类可读描述（SDK 提供 GXGetLastError，无需自己查表）
std::string GetErrorString(GX_STATUS status)
{
  char * error_info = nullptr;
  size_t size = 0;
  // 第一次调用只取字符串长度
  if (GXGetLastError(&status, nullptr, &size) != GX_STATUS_SUCCESS) {
    return "<Error when calling GXGetLastError>";
  }
  error_info = new char[size];
  // 第二次调用取内容
  GX_STATUS s = GXGetLastError(&status, error_info, &size);
  std::string msg = error_info != nullptr ? error_info : "";
  delete[] error_info;
  return s == GX_STATUS_SUCCESS ? msg : "<Error when calling GXGetLastError>";
}

}  // namespace

namespace traditional_armor
{

DahengCamera::DahengCamera() = default;

DahengCamera::~DahengCamera() { Close(); }

bool DahengCamera::InitLib()
{
  GX_STATUS status = GXInitLib();
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("GXInitLib 失败: {}", GetErrorString(status));
    return false;
  }
  lib_initialized_ = true;
  return true;
}

std::vector<DeviceInfo> DahengCamera::EnumerateDevices()
{
  std::vector<DeviceInfo> devices;

  uint32_t device_num = 0;
  // GigE 相机响应慢，枚举超时官方建议至少 1000ms
  GX_STATUS status = GXUpdateAllDeviceList(&device_num, 1000);
  if (status != GX_STATUS_SUCCESS || device_num == 0) {
    RM_LOG_ERROR("枚举失败或未找到设备: {}", GetErrorString(status));
    RM_LOG_ERROR("请检查: 1) 相机上电 2) USB3.0 口 3) 设备权限");
    return devices;
  }

  RM_LOG_INFO("共找到 {} 台设备:", device_num);
  for (uint32_t i = 1; i <= device_num; ++i) {  // 大恒设备序号从 1 开始
    GX_DEVICE_INFO info;
    memset(&info, 0, sizeof(info));
    if (GXGetDeviceInfo(i, &info) != GX_STATUS_SUCCESS) {
      continue;
    }
    // 只支持 USB3 相机（U3V）
    if (info.emDevType == GX_DEVICE_CLASS_U3V) {
      auto & u3v = info.DevInfo.stU3VDevInfo;
      // SDK 字段是 unsigned char[64]，显式强转成 std::string
      std::string model = reinterpret_cast<const char *>(u3v.chModelName);
      std::string serial = reinterpret_cast<const char *>(u3v.chSerialNumber);
      RM_LOG_INFO("[{}] 型号: {}  序列号: {}", i, model, serial);
      devices.push_back({serial, model});
    }
  }
  return devices;
}

bool DahengCamera::Open(const std::string & serial)
{
  // 枚举后按序列号定位 1 开始的大恒设备序号
  auto devices = EnumerateDevices();
  int index = -1;
  for (size_t i = 0; i < devices.size(); ++i) {
    if (devices[i].serial == serial) {
      index = static_cast<int>(i) + 1;
      break;
    }
  }
  if (index < 0) {
    RM_LOG_ERROR("序列号 {} 不在枚举列表中!", serial);
    return false;
  }

  GX_STATUS status = GXOpenDeviceByIndex(index, &device_);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("GXOpenDeviceByIndex: {}", GetErrorString(status));
    return false;
  }
  device_opened_ = true;
  RM_LOG_INFO("打开相机 {} 成功", serial);

  // 1. 自动挡全关（调参前必须关）
  GXSetEnumValueByString(device_, "ExposureAuto", "Off");
  GXSetEnumValueByString(device_, "GainAuto", "Off");
  GXSetEnumValueByString(device_, "BalanceWhiteAuto", "Continuous");

  // 2. 连续采集 + 关触发
  status = GXSetEnumValueByString(device_, "AcquisitionMode", "Continuous");
  if (status != GX_STATUS_SUCCESS) RM_LOG_ERROR("AcquisitionMode: {}", GetErrorString(status));
  status = GXSetEnumValueByString(device_, "TriggerMode", "Off");
  if (status != GX_STATUS_SUCCESS) RM_LOG_ERROR("TriggerMode: {}", GetErrorString(status));

  // 3. 像素格式：传感器原始 Bayer RG8
  status = GXSetEnumValue(device_, "PixelFormat", GX_PIXEL_FORMAT_BAYER_RG8);
  if (status != GX_STATUS_SUCCESS) RM_LOG_ERROR("PixelFormat: {}", GetErrorString(status));

  // 4. USB3 相机出厂限速，把 DeviceLinkThroughputLimit 拉到最大，否则帧率上不去
  GX_INT_VALUE limit_node;
  memset(&limit_node, 0, sizeof(limit_node));
  if (GXGetIntValue(device_, "DeviceLinkThroughputLimit", &limit_node) == GX_STATUS_SUCCESS) {
    status = GXSetIntValue(device_, "DeviceLinkThroughputLimit", limit_node.nMax);
    if (status != GX_STATUS_SUCCESS)
      RM_LOG_ERROR("DeviceLinkThroughputLimit: {}", GetErrorString(status));
  }

  // 5. 读分辨率（设置 PixelFormat 之后再读）
  GX_INT_VALUE width_node, height_node;
  memset(&width_node, 0, sizeof(width_node));
  memset(&height_node, 0, sizeof(height_node));
  GXGetIntValue(device_, "Width", &width_node);
  GXGetIntValue(device_, "Height", &height_node);
  width_ = static_cast<int>(width_node.nCurValue);
  height_ = static_cast<int>(height_node.nCurValue);
  RM_LOG_INFO("分辨率 {}x{}", width_, height_);

  // 6. Bayer 转换前查滤镜排列（RG/GB/GR/BG），传错会导致颜色错乱
  GX_ENUM_VALUE filter_value;
  memset(&filter_value, 0, sizeof(filter_value));
  if (GXGetEnumValue(device_, "PixelColorFilter", &filter_value) == GX_STATUS_SUCCESS) {
    color_filter_ = filter_value.stCurValue.nCurValue;
  }

  return true;
}

bool DahengCamera::StartStream()
{
  // 大恒的"流"概念：payload（单帧字节数）从数据流句柄查询，本项目只取 1 号流
  uint32_t stream_num = 0;
  if (GXGetDataStreamNumFromDev(device_, &stream_num) != GX_STATUS_SUCCESS || stream_num < 1) {
    RM_LOG_ERROR("获取数据流失败");
    return false;
  }
  GXGetDataStreamHandleFromDev(device_, 1, &stream_handle_);

  // SDK 内部取流缓存 5 个，并分配 Bayer -> RGB 转换输出缓冲
  GXSetAcqusitionBufferNumber(device_, 5);
  // [优化] 分配两块转换缓冲，交替使用
  rgb_buffer_[0] = static_cast<unsigned char *>(malloc(sizeof(unsigned char) * width_ * height_ * 3));
  rgb_buffer_[1] = static_cast<unsigned char *>(malloc(sizeof(unsigned char) * width_ * height_ * 3));
  rgb_buffer_index_ = 0;

  GX_STATUS status = GXStreamOn(device_);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("GXStreamOn: {}", GetErrorString(status));
    free(rgb_buffer_[0]);
    free(rgb_buffer_[1]);
    rgb_buffer_[0] = nullptr;
    rgb_buffer_[1] = nullptr;
    return false;
  }
  streaming_ = true;
  RM_LOG_INFO("取流开始");
  return true;
}

bool DahengCamera::Grab(cv::Mat & image, int timeout_ms)
{
  PGX_FRAME_BUFFER frame_buffer = nullptr;
  // DQ = DeQueue，从 SDK 队列取出一帧
  GX_STATUS status = GXDQBuf(device_, &frame_buffer, timeout_ms);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("GXDQBuf 超时/失败: {}", GetErrorString(status));
    return false;
  }

  // 帧状态检查：丢包/传输错误的帧跳过，但必须照常 QB 还回去
  if (frame_buffer->nStatus != GX_FRAME_STATUS_SUCCESS) {
    RM_LOG_WARN("帧状态异常: {:#x}", static_cast<unsigned int>(frame_buffer->nStatus));
    GXQBuf(device_, frame_buffer);
    return false;
  }

  // Bayer RG8 -> BGR24（邻域插值，不翻转，输出 OpenCV BGR 通道序）
  // [优化] 写入当前缓冲，双缓冲交替使用
  unsigned char * rgb_out = rgb_buffer_[rgb_buffer_index_];
  VxInt32 dx_status = DxRaw8toRGB24Ex(
    frame_buffer->pImgBuf, rgb_out, frame_buffer->nWidth, frame_buffer->nHeight,
    RAW2RGB_NEIGHBOUR, DX_PIXEL_COLOR_FILTER(color_filter_), false, DX_ORDER_BGR);
  if (dx_status != DX_OK) {
    RM_LOG_ERROR("DxRaw8toRGB24Ex 失败: {:#x}", static_cast<unsigned int>(dx_status));
    GXQBuf(device_, frame_buffer);
    return false;
  }

  // [优化] 直接共享转换缓冲（不 clone），下次取帧改用另一块缓冲，
  //        上一帧在下次取帧前仍有效（主循环为单消费者，逐帧使用）
  image = cv::Mat(frame_buffer->nHeight, frame_buffer->nWidth, CV_8UC3, rgb_out);
  rgb_buffer_index_ ^= 1;  // 切换缓冲

  // QB = EnQueue，用完还回队列，漏还会把队列掏空导致帧率掉 0
  GXQBuf(device_, frame_buffer);
  return true;
}

void DahengCamera::StopStream()
{
  if (streaming_) {
    GXStreamOff(device_);
    streaming_ = false;
  }
  // [优化] 释放两块转换缓冲
  for (int i = 0; i < 2; i++) {
    if (rgb_buffer_[i]) {
      free(rgb_buffer_[i]);
      rgb_buffer_[i] = nullptr;
    }
  }
}

void DahengCamera::Close()
{
  StopStream();
  if (device_opened_) {
    GXCloseDevice(device_);
    device_opened_ = false;
    device_ = nullptr;
    RM_LOG_INFO("相机已关闭");
  }
  if (lib_initialized_) {
    GXCloseLib();
    lib_initialized_ = false;
  }
}

bool DahengCamera::AddExposureTime(double delta_us)
{
  GX_FLOAT_VALUE node;
  memset(&node, 0, sizeof(node));
  GX_STATUS status = GXGetFloatValue(device_, "ExposureTime", &node);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("Get ExposureTime: {}", GetErrorString(status));
    return false;
  }

  double value = node.dCurValue + delta_us;
  value = value < 1.0 ? 1.0 : (value > 10000.0 ? 10000.0 : value);

  status = GXSetFloatValue(device_, "ExposureTime", value);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("Set ExposureTime: {}", GetErrorString(status));
    return false;
  }
  RM_LOG_INFO("曝光时间 -> {:.2f} us", value);
  return true;
}

bool DahengCamera::AddGain(double delta_db)
{
  GX_FLOAT_VALUE node;
  memset(&node, 0, sizeof(node));
  GX_STATUS status = GXGetFloatValue(device_, "Gain", &node);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("Get Gain: {}", GetErrorString(status));
    return false;
  }

  double value = node.dCurValue + delta_db;
  value = value < 0.0 ? 0.0 : (value > 32.0 ? 32.0 : value);

  status = GXSetFloatValue(device_, "Gain", value);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("Set Gain: {}", GetErrorString(status));
    return false;
  }
  RM_LOG_INFO("增益 -> {:.2f} dB", value);
  return true;
}

bool DahengCamera::AddGamma(double delta)
{
  GX_FLOAT_VALUE node;
  memset(&node, 0, sizeof(node));
  GX_STATUS status = GXGetFloatValue(device_, "Gamma", &node);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("Get Gamma: {}", GetErrorString(status));
    return false;
  }

  double value = node.dCurValue + delta;
  value = value < 0.1 ? 0.1 : (value > 3.0 ? 3.0 : value);

  status = GXSetFloatValue(device_, "Gamma", value);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("Set Gamma: {}", GetErrorString(status));
    return false;
  }
  RM_LOG_INFO("伽马 -> {:.2f}", value);
  return true;
}

// [优化] 绝对设置固定曝光（短曝光解除相机出帧率上限，需按现场光照标定）
bool DahengCamera::SetExposure(double us)
{
  double value = us < 1.0 ? 1.0 : (us > 10000.0 ? 10000.0 : us);
  GX_STATUS status = GXSetFloatValue(device_, "ExposureTime", value);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("Set ExposureTime: {}", GetErrorString(status));
    return false;
  }
  RM_LOG_INFO("曝光时间 -> {:.2f} us", value);
  return true;
}

// [优化] 绝对设置固定增益
bool DahengCamera::SetGain(double db)
{
  double value = db < 0.0 ? 0.0 : (db > 32.0 ? 32.0 : db);
  GX_STATUS status = GXSetFloatValue(device_, "Gain", value);
  if (status != GX_STATUS_SUCCESS) {
    RM_LOG_ERROR("Set Gain: {}", GetErrorString(status));
    return false;
  }
  RM_LOG_INFO("增益 -> {:.2f} dB", value);
  return true;
}

}  // namespace traditional_armor
