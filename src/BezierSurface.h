#pragma once

#include <glm/glm.hpp>
#include <vector>

// 贝塞尔曲面数学与网格生成
// 暴露给 UI 的 4×4 控制点
extern glm::vec3 controlPoints[4][4];

// 伯恩斯坦基多项式 B(i, 3, t) 
float Bernstein(int i, float t);

// 在参数 (u, v) 处评估贝塞尔曲面片
glm::vec3 EvaluateBezierPatch(float u, float v);

// 计算关于 u 的偏导数
glm::vec3 EvaluateBezierPatchDu(float u, float v);

// 计算关于 v 的偏导数
glm::vec3 EvaluateBezierPatchDv(float u, float v);

// 生成完整三角形网格（顶点 + 索引 + 法线）
// resolution：每条边的四边形数量
void GenerateSurfaceMesh(int resolution,
                         std::vector<float> &vertices,
                         std::vector<unsigned int> &indices);

// 更新 GPU 上的 VBO / EBO（在控制点更改后调用）
void UpdateSurfaceBuffers(unsigned int VAO, unsigned int VBO, unsigned int EBO,
                          int resolution, unsigned int &indexCount);

// 将控制点初始化为默认曲面形状
void InitDefaultControlPoints();
