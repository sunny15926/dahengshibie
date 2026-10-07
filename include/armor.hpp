#pragma once

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace traditional_armor
{

// 装甲板颜色。传统识别只分割敌方颜色，因此所有灯条颜色一致。
enum class Color
{
  red,
  blue
};

// 灯条：装甲板左右两侧的发光竖条，是传统识别的最小单元。
struct Lightbar
{
  std::size_t id;              // 灯条编号（按被接受为灯条的顺序递增）
  Color color;                 // 灯条颜色（= 敌方颜色）
  cv::Point2f center;          // 灯条中心（RotatedRect 中心）
  cv::Point2f top, bottom;     // 灯条上下两端的中点
  cv::Point2f top2bottom;      // 从 top 指向 bottom 的向量
  double angle;                // 灯条朝向角：top2bottom 与 x 轴夹角（rad）
  double angle_error;          // 灯条偏离竖直方向的角度 |angle - π/2|（rad）
  double length;               // 灯条长度（像素）
  double width;                // 灯条宽度（像素）
  double ratio;                // 灯条长宽比 = length / width
  cv::RotatedRect rotated_rect;  // 灯条最小外接旋转矩形

  Lightbar() = default;
  Lightbar(const cv::RotatedRect & rotated_rect, std::size_t id);
};

// 装甲板：由左右两个同色灯条配对而成。
struct Armor
{
  Color color;                 // 装甲板颜色（与灯条一致）
  Lightbar left, right;        // 左右灯条
  cv::Point2f center;          // 两灯条中心连线的中点（不是四角形对角线交点，不能作实际中心）
  std::vector<cv::Point2f> points;  // 装甲板四个角点：左上、右上、右下、左下

  double ratio;                // 装甲板宽高比 = 两灯条中心距离 / 长灯条长度
  double side_ratio;           // 左右灯条长度比 = 长灯条 / 短灯条
  double rectangular_error;    // 灯条与两灯条中心连线所成夹角与 π/2 的差值（rad）

  Armor(const Lightbar & left, const Lightbar & right);
};

// 灯条构造：由最小外接旋转矩形计算几何属性（移植自原 armor.cpp）
inline Lightbar::Lightbar(const cv::RotatedRect & rotated_rect, std::size_t id)
: id(id), rotated_rect(rotated_rect)
{
  // 取出旋转矩形的四个角点
  std::vector<cv::Point2f> corners(4);
  rotated_rect.points(&corners[0]);

  // 按 y 坐标升序排序：前两个是上边两点，后两个是下边两点
  std::sort(
    corners.begin(), corners.end(),
    [](const cv::Point2f & a, const cv::Point2f & b) { return a.y < b.y; });

  center = rotated_rect.center;
  // 上端中点 = 上边两点的中点；下端中点 = 下边两点的中点
  top = (corners[0] + corners[1]) / 2;
  bottom = (corners[2] + corners[3]) / 2;
  top2bottom = bottom - top;

  width = cv::norm(corners[0] - corners[1]);  // 上边长度即灯条宽度
  angle = std::atan2(top2bottom.y, top2bottom.x);
  angle_error = std::abs(angle - CV_PI / 2);  // 偏离竖直方向的角度
  length = cv::norm(top2bottom);              // 灯条长度
  ratio = length / width;                     // 长宽比
}

// 装甲板构造：由左右灯条配对（移植自原 armor.cpp）
inline Armor::Armor(const Lightbar & left, const Lightbar & right)
: left(left), right(right)
{
  color = left.color;
  center = (left.center + right.center) / 2;

  // 四个角点：左上、右上、右下、左下
  points.emplace_back(left.top);
  points.emplace_back(right.top);
  points.emplace_back(right.bottom);
  points.emplace_back(left.bottom);

  auto left2right = right.center - left.center;
  auto width = cv::norm(left2right);  // 两灯条中心距离
  auto max_lightbar_length = std::max(left.length, right.length);
  auto min_lightbar_length = std::min(left.length, right.length);
  ratio = width / max_lightbar_length;                      // 装甲板宽高比
  side_ratio = max_lightbar_length / min_lightbar_length;   // 左右灯条长度比

  // 两灯条中心连线方向角 roll；灯条应垂直于该连线
  auto roll = std::atan2(left2right.y, left2right.x);
  auto left_rectangular_error = std::abs(left.angle - roll - CV_PI / 2);
  auto right_rectangular_error = std::abs(right.angle - roll - CV_PI / 2);
  rectangular_error = std::max(left_rectangular_error, right_rectangular_error);
}

}  // namespace traditional_armor
