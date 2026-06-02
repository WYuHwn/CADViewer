#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

// Mesh
// 持有一个可绘制的子网格，包含其自身的 VAO / VBO / EBO。
// Assimp 模型加载与 VBO 管理
struct Mesh
{
    unsigned int VAO        = 0;
    unsigned int VBO        = 0;
    unsigned int EBO        = 0;
    unsigned int indexCount = 0;
    unsigned int vertexCount = 0;

    // 完整的原始交错顶点数据：
    // [px,py,pz, nx,ny,nz, tu,tv] × vertexCount
    std::vector<float> originalVertices;

    Mesh() = default;

    // 将交错顶点数据和索引数据上传至 GPU
    void setup(const std::vector<float>        &vertices,
               const std::vector<unsigned int> &indices);

    void draw() const;

    // 释放 GPU 资源
    void destroy();
};

// Model
// 通过 Assimp 加载 3D 文件（STL / OBJ / …）并为渲染准备所有网格。
// 同时计算包围盒，以便调用者可以进行自动居中和自动缩放。
class Model
{
public:
    Model() = default;
    ~Model();

    // 从磁盘加载；成功返回 true
    bool load(const std::string &path);

    // 绘制模型中的所有网格
    void draw() const;

    // 释放所有 GPU 缓冲
    void destroy();

    // FFD（自由变形）支持
    // 返回模型本地空间中所有顶点位置的扁平数组。
    const std::vector<glm::vec3>& originalPositions() const { return m_origPositions; }
    unsigned int totalVertexCount() const { return (unsigned int)m_origPositions.size(); }

    // 用 newPositions（数量需与 originalPositions 相同）替换每个
    // 顶点位置并重新上传 VBO。法线和纹理坐标保持不变。
    void applyDeformation(const std::vector<glm::vec3> &newPositions);

    // 包围盒访问器（成功加载后有效）
    glm::vec3 minBounds() const { return m_minBounds; }
    glm::vec3 maxBounds() const { return m_maxBounds; }
    glm::vec3 center()     const { return (m_minBounds + m_maxBounds) * 0.5f; }
    glm::vec3 size()       const { return m_maxBounds - m_minBounds; }
    float     maxExtent()  const;
    int       triangleCount() const { return m_triCount; }

private:
    std::vector<Mesh> meshes;
    glm::vec3 m_minBounds{0.0f};
    glm::vec3 m_maxBounds{0.0f};
    int       m_triCount = 0;

    // FFD：原始顶点位置的扁平数组（模型本地空间）
    std::vector<glm::vec3> m_origPositions;

    // 递归处理 Assimp 节点树
    void processNode(void *aiNode, const void *aiScene);
    Mesh processMesh(void *aiMesh, const void *aiScene);
    void updateBounds(const glm::vec3 &v);
};
