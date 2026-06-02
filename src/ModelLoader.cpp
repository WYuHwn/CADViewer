#include "ModelLoader.h"
#include <glad/glad.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <iostream>

// Mesh::setup
void Mesh::setup(const std::vector<float>        &vertices,
                 const std::vector<unsigned int> &indices)
{
    indexCount  = static_cast<unsigned int>(indices.size());
    vertexCount = static_cast<unsigned int>(vertices.size() / 8); // 每个顶点 8 个浮点数

    // 保留一份副本用于 FFD
    originalVertices = vertices;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(float),
                 vertices.data(),
                 GL_STATIC_DRAW);

    // EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(unsigned int),
                 indices.data(),
                 GL_STATIC_DRAW);

    // 顶点属性布局（与顶点着色器一致）：
    // location 0 : vec3 位置
    // location 1 : vec3 法线
    // location 2 : vec2 纹理坐标
    const int stride = 8 * sizeof(float);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

// Mesh::draw
void Mesh::draw() const
{
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// Mesh::destroy
void Mesh::destroy()
{
    if (EBO) { glDeleteBuffers(1, &EBO); EBO = 0; }
    if (VBO) { glDeleteBuffers(1, &VBO); VBO = 0; }
    if (VAO) { glDeleteVertexArrays(1, &VAO); VAO = 0; }
    indexCount = 0;
}

// Model::load
bool Model::load(const std::string &path)
{
    Assimp::Importer importer;

    // 后处理标志
    const unsigned int ppFlags =
        aiProcess_Triangulate           |
        aiProcess_GenNormals            |   // 如果缺失则创建法线
        aiProcess_JoinIdenticalVertices |
        aiProcess_PreTransformVertices  ;   // 展平节点层级

    const aiScene *scene = importer.ReadFile(path, ppFlags);
    if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
        std::cerr << "Assimp error: " << importer.GetErrorString() << "\n";
        return false;
    }

    // 递归处理场景
    processNode(scene->mRootNode, scene);
    return true;
}

// Model::processNode
void Model::processNode(void *aiNodePtr, const void *aiScenePtr)
{
    auto *node  = static_cast<aiNode*>(aiNodePtr);
    auto *scene = static_cast<const aiScene*>(aiScenePtr);

    // 处理此节点中的所有网格（Mesh）
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        unsigned int meshIdx    = node->mMeshes[i];
        aiMesh      *assimpMesh = scene->mMeshes[meshIdx];
        Mesh mesh = processMesh(assimpMesh, scene);
        meshes.push_back(mesh);
    }

    // 递归处理子节点
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        processNode(node->mChildren[i], scene);
    }
}

// Model::updateBounds
void Model::updateBounds(const glm::vec3 &v)
{
    if (v.x < m_minBounds.x) m_minBounds.x = v.x;
    if (v.y < m_minBounds.y) m_minBounds.y = v.y;
    if (v.z < m_minBounds.z) m_minBounds.z = v.z;
    if (v.x > m_maxBounds.x) m_maxBounds.x = v.x;
    if (v.y > m_maxBounds.y) m_maxBounds.y = v.y;
    if (v.z > m_maxBounds.z) m_maxBounds.z = v.z;
}

// Model::maxExtent
float Model::maxExtent() const
{
    glm::vec3 s = size();
    return std::max({s.x, s.y, s.z});
}

// Model::processMesh
Mesh Model::processMesh(void *aiMeshPtr, const void * /*aiScenePtr*/)
{
    auto *meshData = static_cast<aiMesh*>(aiMeshPtr);

    std::vector<float>        vertices;
    std::vector<unsigned int> indices;

    // 顶点
    for (unsigned int i = 0; i < meshData->mNumVertices; ++i) {
        // 位置
        vertices.push_back(meshData->mVertices[i].x);
        vertices.push_back(meshData->mVertices[i].y);
        vertices.push_back(meshData->mVertices[i].z);

        // 追踪包围盒并存储原始位置供 FFD 使用
        updateBounds(glm::vec3(meshData->mVertices[i].x,
                               meshData->mVertices[i].y,
                               meshData->mVertices[i].z));
        m_origPositions.push_back(glm::vec3(meshData->mVertices[i].x,
                                            meshData->mVertices[i].y,
                                            meshData->mVertices[i].z));

        // 法线（由 aiProcess_GenNormals 保证存在）
        if (meshData->HasNormals()) {
            vertices.push_back(meshData->mNormals[i].x);
            vertices.push_back(meshData->mNormals[i].y);
            vertices.push_back(meshData->mNormals[i].z);
        } else {
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
            vertices.push_back(1.0f);
        }

        // 纹理坐标（仅第一通道）
        if (meshData->mTextureCoords[0]) {
            vertices.push_back(meshData->mTextureCoords[0][i].x);
            vertices.push_back(meshData->mTextureCoords[0][i].y);
        } else {
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
        }
    }

    // 索引
    for (unsigned int i = 0; i < meshData->mNumFaces; ++i) {
        const aiFace &face = meshData->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            indices.push_back(face.mIndices[j]);
        }
    }
    m_triCount += meshData->mNumFaces;

    // 上传至 GPU
    Mesh mesh;
    mesh.setup(vertices, indices);
    return mesh;
}

// Model::draw
void Model::draw() const
{
    for (const Mesh &m : meshes) {
        m.draw();
    }
}

// Model::destroy
void Model::destroy()
{
    for (Mesh &m : meshes) {
        m.destroy();
    }
    meshes.clear();
}

// Model::applyDeformation
// 将每个网格 VBO 中的位置分量替换为 newPositions。
// newPositions 的数量和顺序必须与 originalPositions() 完全一致。
void Model::applyDeformation(const std::vector<glm::vec3> &newPositions)
{
    if (newPositions.size() != m_origPositions.size()) return;

    unsigned int posIdx = 0;  // newPositions 的索引（扁平化，跨所有网格）

    for (Mesh &m : meshes) {
        // 构建新的交错顶点数据
        std::vector<float> data = m.originalVertices;  // 复制
        for (unsigned int v = 0; v < m.vertexCount; ++v) {
            unsigned int base = v * 8;                  // 每个顶点 8 个浮点数
            data[base + 0] = newPositions[posIdx].x;
            data[base + 1] = newPositions[posIdx].y;
            data[base + 2] = newPositions[posIdx].z;
            ++posIdx;
        }
        // 重新上传
        glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        data.size() * sizeof(float), data.data());
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Model::~Model
Model::~Model()
{
    destroy();
}
