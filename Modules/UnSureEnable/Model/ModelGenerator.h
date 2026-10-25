#pragma once

#include <string>

namespace X_Y::Model {

/// @brief 在指定路径生成长方体 .obj 文件（每个面独立法线）
/// @param width   X 轴方向长度
/// @param height  Y 轴方向长度
/// @param depth   Z 轴方向长度
/// @param outputPath 输出 .obj 文件路径
/// @param err  错误信息
/// @return true 成功
bool GenerateBoxOBJ(float width, float height, float depth,
                    const std::string& outputPath, std::string& err);
} // namespace X_Y::Model
