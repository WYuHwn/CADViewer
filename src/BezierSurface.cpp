#include "BezierSurface.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

// 16个控制点（4×4网格）
glm::vec3 controlPoints[4][4];

// 伯恩斯坦基 B(i, 3, t)
float Bernstein(int i, float t)
{
    switch (i) {
    case 0: return (1.0f - t) * (1.0f - t) * (1.0f - t);    // (1-t)³
    case 1: return 3.0f * t * (1.0f - t) * (1.0f - t);  // 3t(1-t)²
    case 2: return 3.0f * t * t * (1.0f - t);   // 3t²(1-t)
    case 3: return t * t * t;   // t³
    default: return 0.0f;
    }
}

// 伯恩斯坦基的一阶导数 B'(i, 3, t)
static float BernsteinDerivative(int i, float t)
{
    switch (i) {
    case 0: return -3.0f * (1.0f - t) * (1.0f - t);     // -3(1-t)²
    case 1: return 3.0f * (1.0f - t) * (1.0f - 3.0f * t);   // 3(1-t)(1-3t)
    case 2: return 3.0f * t * (2.0f - 3.0f * t);    // 3t(2-3t)
    case 3: return 3.0f * t * t;    // 3t²
    default: return 0.0f;
    }
}

// 计算贝塞尔曲面片上的点
glm::vec3 EvaluateBezierPatch(float u, float v)
{
    glm::vec3 p(0.0f);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            p += controlPoints[i][j] * Bernstein(i, u) * Bernstein(j, v);
        }
    }
    return p;
}

// 偏导数 ∂P/∂u
glm::vec3 EvaluateBezierPatchDu(float u, float v)
{
    glm::vec3 d(0.0f);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            d += controlPoints[i][j] * BernsteinDerivative(i, u) * Bernstein(j, v);
        }
    }
    return d;
}

// 偏导数 ∂P/∂v
glm::vec3 EvaluateBezierPatchDv(float u, float v)
{
    glm::vec3 d(0.0f);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            d += controlPoints[i][j] * Bernstein(i, u) * BernsteinDerivative(j, v);
        }
    }
    return d;
}

// 生成完整三角形网格
void GenerateSurfaceMesh(int resolution,
                         std::vector<float> &vertices,
                         std::vector<unsigned int> &indices)
{
    vertices.clear();
    indices.clear();

    int gridSize = resolution + 1;

    // 顶点（位置 + 法线）
    for (int i = 0; i <= resolution; ++i) {
        float u = (float)i / (float)resolution;
        for (int j = 0; j <= resolution; ++j) {
            float v = (float)j / (float)resolution;

            glm::vec3 pos = EvaluateBezierPatch(u, v);

            // 法线 = cross(∂P/∂u, ∂P/∂v)，归一化
            glm::vec3 du = EvaluateBezierPatchDu(u, v);
            glm::vec3 dv = EvaluateBezierPatchDv(u, v);
            glm::vec3 n  = glm::normalize(glm::cross(du, dv));

            // 防止退化法线
            if (glm::length(glm::cross(du, dv)) < 1e-6f) {
                n = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            vertices.push_back(pos.x);
            vertices.push_back(pos.y);
            vertices.push_back(pos.z);
            vertices.push_back(n.x);
            vertices.push_back(n.y);
            vertices.push_back(n.z);
            // 曲面片不使用纹理坐标
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
        }
    }

    // 三角形索引
    for (int i = 0; i < resolution; ++i) {
        for (int j = 0; j < resolution; ++j) {
            unsigned int idx00 = i * gridSize + j;
            unsigned int idx10 = (i + 1) * gridSize + j;
            unsigned int idx01 = i * gridSize + (j + 1);
            unsigned int idx11 = (i + 1) * gridSize + (j + 1);

            // 每个四边形两个三角形
            indices.push_back(idx00);
            indices.push_back(idx10);
            indices.push_back(idx11);

            indices.push_back(idx00);
            indices.push_back(idx11);
            indices.push_back(idx01);
        }
    }
}

// 上传网格数据至 GPU 缓冲
void UpdateSurfaceBuffers(unsigned int VAO, unsigned int VBO, unsigned int EBO,
                          int resolution, unsigned int &indexCount)
{
    std::vector<float>  vertices;
    std::vector<unsigned int> indices;

    GenerateSurfaceMesh(resolution, vertices, indices);
    indexCount = (unsigned int)indices.size();

    glBindVertexArray(VAO);

    // 上传顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(float),
                 vertices.data(),
                 GL_DYNAMIC_DRAW);

    // 上传索引数据
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(unsigned int),
                 indices.data(),
                 GL_DYNAMIC_DRAW);

    // 属性布局：位置(3f) + 法线(3f) + 纹理坐标(2f) = 8 个浮点数
    const int stride = 8 * sizeof(float);

    // 位置 – location 0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);

    // 法线 – location 1
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 纹理坐标 – location 2
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

// 默认控制点布局（一个平缓的穹顶形曲面片）
void InitDefaultControlPoints()
{
    // 以原点上方为中心的 4×4 网格，带有轻微曲率
    for (int i = 0; i < 4; ++i) {
        float x = -1.5f + 1.0f * (float)i;  // x ∈ [-1.5, +1.5]
        for (int j = 0; j < 4; ++j) {
            float z = -1.5f + 1.0f * (float)j;  // z ∈ [-1.5, +1.5]

            // 高度 (y) 形成穹顶：中心最高，边缘较低
            float cx = x / 1.5f;   // 归一化到 [-1, 1]
            float cz = z / 1.5f;
            float y  = 1.5f - 0.5f * (cx * cx + cz * cz);  // 穹顶

            controlPoints[i][j] = glm::vec3(x, y, z);
        }
    }
}
