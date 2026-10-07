#include "traditional_detector.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>

#include "rm_log.hpp"

namespace traditional_armor
{

TraditionalDetector::TraditionalDetector(const std::string & config_path)
{
  auto yaml = YAML::LoadFile(config_path);

  // 敌方颜色
  auto enemy_color_str = yaml["enemy_color"].as<std::string>();
  enemy_color_ = enemy_color_str == "red" ? Color::red : Color::blue;

  // 红色 HSV 阈值：红色在色相环上跨越 0°/180° 两端，用两段区间取并集
  red_hue_low_[0] = yaml["red_hue_low"][0].as<int>();
  red_hue_high_[0] = yaml["red_hue_high"][0].as<int>();
  red_hue_low_[1] = yaml["red_hue_low"][1].as<int>();
  red_hue_high_[1] = yaml["red_hue_high"][1].as<int>();
  // 蓝色 HSV 阈值：只用一段区间
  blue_hue_low_ = yaml["blue_hue_low"].as<int>();
  blue_hue_high_ = yaml["blue_hue_high"].as<int>();

  saturation_low_ = yaml["saturation_low"].as<int>();
  saturation_high_ = yaml["saturation_high"].as<int>();
  value_low_ = yaml["value_low"].as<int>();
  value_high_ = yaml["value_high"].as<int>();
  morph_kernel_size_ = yaml["morph_kernel_size"].as<int>();

  // 灯条筛选阈值（角度由 degree 转 rad）
  max_angle_error_ = yaml["max_angle_error"].as<double>() / 57.3;
  min_lightbar_ratio_ = yaml["min_lightbar_ratio"].as<double>();
  max_lightbar_ratio_ = yaml["max_lightbar_ratio"].as<double>();
  min_lightbar_length_ = yaml["min_lightbar_length"].as<double>();

  // 装甲配对判断阈值
  min_armor_ratio_ = yaml["min_armor_ratio"].as<double>();
  max_armor_ratio_ = yaml["max_armor_ratio"].as<double>();
  max_side_ratio_ = yaml["max_side_ratio"].as<double>();
  max_rectangular_error_ = yaml["max_rectangular_error"].as<double>() / 57.3;

  debug_ = yaml["debug"].as<bool>();

  RM_LOG_INFO("TraditionalDetector loaded: enemy_color={}", enemy_color_str);
}

std::vector<Armor> TraditionalDetector::detect(const cv::Mat & bgr_img)
{
  // 1. 图像预处理 + HSV 颜色阈值分割 + 形态学，得到二值图
  auto binary_img = preprocess(bgr_img);
  if (debug_) cv::imshow("binary", binary_img);

  // 2. findContours 提取轮廓 + RotatedRect 筛选灯条
  auto lightbars = extract_lightbars(binary_img);

  // 3. 灯条两两配对组成装甲板
  std::vector<Armor> armors;
  for (std::size_t i = 0; i < lightbars.size(); i++) {
    for (std::size_t j = i + 1; j < lightbars.size(); j++) {
      auto & left = lightbars[i];
      auto & right = lightbars[j];

      // 只配合同色灯条（本模块仅分割敌方颜色，此处为防御性判断）
      if (left.color != right.color) continue;

      auto armor = Armor(left, right);
      if (!check_armor(armor)) continue;

      armors.emplace_back(armor);
    }
  }

  // 4. 去重：两块装甲共用同一灯条时，保留 rectangular_error 更小（更接近矩形）者。
  //    （原实现依赖分类器置信度去重，传统识别用几何误差替代）
  std::vector<bool> duplicated(armors.size(), false);
  for (std::size_t i = 0; i < armors.size(); i++) {
    for (std::size_t j = i + 1; j < armors.size(); j++) {
      auto & a = armors[i];
      auto & b = armors[j];

      auto share = a.left.id == b.left.id || a.left.id == b.right.id ||
                   a.right.id == b.left.id || a.right.id == b.right.id;
      if (!share) continue;

      if (a.rectangular_error <= b.rectangular_error)
        duplicated[j] = true;
      else
        duplicated[i] = true;
    }
  }

  std::vector<Armor> result;
  for (std::size_t i = 0; i < armors.size(); i++) {
    if (!duplicated[i]) result.emplace_back(armors[i]);
  }

  return result;
}

cv::Mat TraditionalDetector::preprocess(const cv::Mat & bgr_img) const
{
  // 1. BGR 转 HSV
  cv::Mat hsv_img;
  cv::cvtColor(bgr_img, hsv_img, cv::COLOR_BGR2HSV);

  // 2. 颜色阈值分割：只保留敌方颜色
  cv::Mat mask;
  if (enemy_color_ == Color::red) {
    // 红色跨越色相环 0°/180° 两端，用两段区间取并集
    cv::Mat mask1, mask2;
    cv::inRange(
      hsv_img, cv::Scalar(red_hue_low_[0], saturation_low_, value_low_),
      cv::Scalar(red_hue_high_[0], saturation_high_, value_high_), mask1);
    cv::inRange(
      hsv_img, cv::Scalar(red_hue_low_[1], saturation_low_, value_low_),
      cv::Scalar(red_hue_high_[1], saturation_high_, value_high_), mask2);
    cv::bitwise_or(mask1, mask2, mask);
  } else {
    // 蓝色只用一段区间
    cv::inRange(
      hsv_img, cv::Scalar(blue_hue_low_, saturation_low_, value_low_),
      cv::Scalar(blue_hue_high_, saturation_high_, value_high_), mask);
  }

  // 3. 形态学闭运算：先膨胀再腐蚀，填补灯条内部空洞、连接细小断裂
  auto kernel =
    cv::getStructuringElement(cv::MORPH_RECT, cv::Size(morph_kernel_size_, morph_kernel_size_));
  cv::dilate(mask, mask, kernel);
  cv::erode(mask, mask, kernel);

  return mask;
}

std::vector<Lightbar> TraditionalDetector::extract_lightbars(const cv::Mat & binary_img)
{
  // 提取外轮廓（只取最外层，灯条是实心亮块）
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(binary_img, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

  // 每个轮廓求最小外接旋转矩形，并按几何条件筛选灯条
  std::size_t lightbar_id = 0;
  std::vector<Lightbar> lightbars;
  for (const auto & contour : contours) {
    auto rotated_rect = cv::minAreaRect(contour);
    auto lightbar = Lightbar(rotated_rect, lightbar_id);

    if (!check_lightbar(lightbar)) continue;

    lightbar.color = enemy_color_;
    lightbars.emplace_back(lightbar);
    lightbar_id += 1;
  }

  // 灯条按中心 x 坐标从左到右排序，便于两两配对
  std::sort(
    lightbars.begin(), lightbars.end(),
    [](const Lightbar & a, const Lightbar & b) { return a.center.x < b.center.x; });

  return lightbars;
}

bool TraditionalDetector::check_lightbar(const Lightbar & lightbar) const
{
  // 灯条应接近竖直：偏离竖直方向的角度小于阈值
  auto angle_ok = lightbar.angle_error < max_angle_error_;
  // 灯条应细长：长宽比落在合理区间
  auto ratio_ok = lightbar.ratio > min_lightbar_ratio_ && lightbar.ratio < max_lightbar_ratio_;
  // 灯条应足够长：排除面积过小的噪点
  auto length_ok = lightbar.length > min_lightbar_length_;
  return angle_ok && ratio_ok && length_ok;
}

bool TraditionalDetector::check_armor(const Armor & armor) const
{
  // 装甲板宽高比合理：两灯条间距与灯条长度的比值
  auto ratio_ok = armor.ratio > min_armor_ratio_ && armor.ratio < max_armor_ratio_;
  // 左右灯条长度接近：长短比小于阈值
  auto side_ratio_ok = armor.side_ratio < max_side_ratio_;
  // 两灯条与中心连线近似垂直
  auto rectangular_error_ok = armor.rectangular_error < max_rectangular_error_;
  return ratio_ok && side_ratio_ok && rectangular_error_ok;
}

void TraditionalDetector::draw(cv::Mat & img, const std::vector<Armor> & armors)
{
  for (const auto & armor : armors) {
    // 绘制左右灯条（黄色）
    cv::line(img, armor.left.top, armor.left.bottom, cv::Scalar(0, 255, 255), 2);
    cv::line(img, armor.right.top, armor.right.bottom, cv::Scalar(0, 255, 255), 2);

    // 绘制装甲板外框（绿色，连接四个角点）
    for (int i = 0; i < 4; i++) {
      cv::line(img, armor.points[i], armor.points[(i + 1) % 4], cv::Scalar(0, 255, 0), 2);
    }

    // 标注中心点（红色实心圆）
    cv::circle(img, armor.center, 3, cv::Scalar(0, 0, 255), -1);
  }
}

}  // namespace traditional_armor
