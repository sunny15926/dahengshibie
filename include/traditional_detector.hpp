#pragma once

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

#include "armor.hpp"

namespace traditional_armor
{

// 传统装甲识别类：输入 BGR 图像，输出装甲目标列表。
// 流水线：图像预处理 → HSV 颜色阈值分割 → 形态学操作 → findContours 提取轮廓
//         → RotatedRect 筛选灯条 → 灯条两两配对组成装甲板。
class TraditionalDetector
{
public:
  explicit TraditionalDetector(const std::string & config_path);

  // 识别入口：输入一帧 BGR 图像，返回识别到的装甲板
  std::vector<Armor> detect(const cv::Mat & bgr_img);

  // 在原图上绘制灯条与装甲框
  void draw(cv::Mat & img, const std::vector<Armor> & armors);

private:
  // ---- 颜色 ----
  Color enemy_color_;                    // 敌方颜色（仅分割该颜色）
  int red_hue_low_[2], red_hue_high_[2];  // 红色跨越 0°/180° 两端，用两段阈值
  int blue_hue_low_, blue_hue_high_;      // 蓝色用一段阈值
  int saturation_low_, saturation_high_;  // 饱和度上下限
  int value_low_, value_high_;            // 明度上下限
  int morph_kernel_size_;                 // 形态学核大小

  // [优化] 预处理复用缓冲区，避免每帧堆分配（识别逻辑不变）
  cv::Mat hsv_img_;         // BGR->HSV 中间图
  cv::Mat mask1_, mask2_;   // 红色两段阈值中间结果
  cv::Mat mask_;            // 最终二值图

  // ---- 灯条筛选阈值 ----
  double max_angle_error_;                         // 灯条偏离竖直方向的最大角度（rad）
  double min_lightbar_ratio_, max_lightbar_ratio_;  // 灯条长宽比范围
  double min_lightbar_length_;                     // 灯条最小长度（像素）

  // ---- 装甲配对判断阈值 ----
  double min_armor_ratio_, max_armor_ratio_;  // 装甲板宽高比范围
  double max_side_ratio_;                     // 左右灯条最大长度比
  double max_rectangular_error_;              // 灯条与连线垂直误差（rad）

  bool debug_;  // 是否显示中间二值图

  // 预处理 + HSV 阈值分割 + 形态学，输出二值图（复用成员缓冲，避免每帧分配）
  cv::Mat preprocess(const cv::Mat & bgr_img);

  // 从二值图提取并筛选灯条
  std::vector<Lightbar> extract_lightbars(const cv::Mat & binary_img);

  // 灯条几何筛选条件
  bool check_lightbar(const Lightbar & lightbar) const;

  // 装甲板配对几何判断条件
  bool check_armor(const Armor & armor) const;
};

}  // namespace traditional_armor
