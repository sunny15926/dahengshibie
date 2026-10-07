# dahengshibie —— 大恒相机传统装甲识别

基于 **大恒（Galaxy）工业相机** 与 **OpenCV** 的传统装甲识别项目，不依赖任何深度学习模型。

识别流水线：

```
图像预处理 → HSV 颜色阈值分割 → 形态学操作 → findContours 提取轮廓
           → RotatedRect 筛选灯条 → 灯条两两配对 → 输出装甲板
```

所有调试打印统一走 `RM_LOG` 宏（基于 spdlog），不使用 `std::cout` / `printf`。

## 目录结构

```
dahengshibie/
├── CMakeLists.txt               # OpenCV + spdlog + yaml-cpp + 大恒 SDK 配置
├── README.md
├── .gitignore
├── config/
│   └── armor.yaml               # HSV 阈值、灯条/装甲几何阈值等全部参数
├── include/
│   ├── rm_log.hpp               # RM_LOG 日志宏（spdlog 封装）
│   ├── armor.hpp                # Color / Lightbar / Armor 数据结构
│   ├── traditional_detector.hpp # 传统装甲识别类（声明）
│   ├── daheng_camera.hpp        # 大恒相机封装类（声明）
│   └── sdk/                     # 大恒 Galaxy SDK 头文件（内置，自包含）
└── src/
    ├── rm_log.cpp               # 日志实现（控制台 + 滚动文件双 sink）
    ├── traditional_detector.cpp # 识别实现（预处理/灯条/配对）
    ├── daheng_camera.cpp        # 相机实现（GxIAPI 取流 + Bayer→BGR）
    └── main.cpp                 # 主循环：取流 → 识别 → 绘制 → 显示
```

## 依赖

| 依赖 | 说明 |
| ---- | ---- |
| OpenCV 4.x | 图像处理、显示 |
| spdlog | 日志（RM_LOG 宏） |
| yaml-cpp | 读取 `config/armor.yaml` |
| 大恒 Galaxy SDK | 相机取流。**头文件已内置**在 `include/sdk/`，动态库 `libgxiapi.so` 由官方驱动装到 `/usr/lib` |

安装系统依赖（Ubuntu）：

```bash
sudo apt install libopencv-dev libspdlog-dev libyaml-cpp-dev
# 大恒 Galaxy SDK 运行时：安装官方 "Galaxy_Linux-x86_64" 包后，libgxiapi.so 会放到 /usr/lib
```

## 编译

```bash
cd dahengshibie
cmake -S . -B build
cmake --build build -j
```

## 运行

相机插入后（无需 udev 规则，本机设备节点已 `rw-rw-rw-`），直接运行：

```bash
./build/dahengshibie config/armor.yaml
```

- `config/armor.yaml` 中 `serial` 留空 `""` 会**自动枚举并打开第一台相机**，也可填序列号指定。
- 窗口 `dahengshibie` 显示实时画面：灯条画黄线、装甲板画绿框、中心画红点；另有 `binary` 窗口显示 HSV 二值分割结果（由 `debug: true` 控制）。
- 按 `q` 退出取流循环，释放相机资源。

## 传统装甲识别完整逻辑

识别类 `TraditionalDetector` 的入口是 `detect(cv::Mat bgr_img)`，输入一帧 BGR 图像，输出 `std::vector<Armor>`。

### 1. 图像预处理 + HSV 颜色阈值分割（`preprocess`）

- `cvtColor` 把 BGR 转 HSV。
- `inRange` 只保留**敌方颜色**（`enemy_color` 配置红/蓝，只分割一种颜色）：
  - **红色**：色相在色相环上跨越 `0°/180°` 两端，故用两段区间 `H∈[0,20] ∪ [160,180]` 做 `inRange` 后 `bitwise_or` 取并集。
  - **蓝色**：色相只在一段区间 `H∈[100,124]`。
  - 同时用饱和度 `S`、明度 `V` 的上下限过滤，排除白色高亮与暗部噪点。
- 输出二值图（灯条为白、背景为黑）。

### 2. 形态学操作

闭运算（先 `dilate` 再 `erode`），核大小由 `morph_kernel_size` 配置：

- 填补灯条内部的细小空洞、连接被过曝/遮挡造成的断裂；
- 滤除孤立的小噪点，让后续轮廓更干净。

### 3. findContours 提取轮廓（`extract_lightbars`）

`cv::findContours(..., RETR_EXTERNAL, CHAIN_APPROX_NONE)`：

- `RETR_EXTERNAL`：只取最外层轮廓，灯条是实心亮块；
- `CHAIN_APPROX_NONE`：保留全部边界点，使 `minAreaRect` 拟合更精确。

### 4. RotatedRect 筛选灯条（`check_lightbar`）

每个轮廓求最小外接旋转矩形 `cv::minAreaRect`，构造 `Lightbar`（由矩形四角点算 top/bottom、朝向角、长度、宽度、长宽比）。筛选条件：

| 条件 | 含义 |
| ---- | ---- |
| `angle_error < max_angle_error`（45°） | 灯条应**接近竖直**，偏离竖直方向的角度不能过大 |
| `min_lightbar_ratio < length/width < max_lightbar_ratio`（1.5~20） | 灯条应**细长**（发光条是长条状） |
| `length > min_lightbar_length`（8px） | 灯条应**足够长**，排除面积过小的噪点 |

通过筛选的灯条颜色置为 `enemy_color_`，并按中心 `x` 坐标从左到右排序，便于两两配对。

### 5. 灯条两两配对组成装甲板（`check_armor`）

双层循环把左右两根同色灯条构造成 `Armor`，几何判断条件：

| 条件 | 含义 |
| ---- | ---- |
| `min_armor_ratio < width/max_len < max_armor_ratio`（1~5） | 装甲板**宽高比合理**：两灯条中心间距与长灯条长度之比 |
| `side_ratio < max_side_ratio`（1.5） | 左右灯条**长度接近**（等长） |
| `rectangular_error < max_rectangular_error`（25°） | 灯条方向与两灯条中心连线**近似垂直**，装甲呈矩形 |

其中 `rectangular_error` 取左右灯条与连线垂直误差的较大值，保证两侧都接近垂直。

### 6. 去重

当两块候选装甲**共用同一根灯条**时（一根灯条被配给多个目标），只保留 `rectangular_error` 更小（几何上更接近矩形）的那块，避免同一根灯条被重复计数。

## 配置说明（`config/armor.yaml`）

- `enemy_color`：`red` / `blue`，只分割该颜色。
- `serial`：相机序列号，留空自动枚举第一台。
- `red_hue_low/high`、`blue_hue_low/high`：红/蓝的色相区间（红为两段）。
- `saturation_low/high`、`value_low/high`：饱和度、明度上下限。
- `morph_kernel_size`：形态学核大小。
- `max_angle_error`、`min/max_lightbar_ratio`、`min_lightbar_length`：灯条筛选阈值。
- `min/max_armor_ratio`、`max_side_ratio`、`max_rectangular_error`：装甲配对阈值。
- `debug`：`true` 显示 HSV 二值图窗口（`binary`），方便现场调阈值。
