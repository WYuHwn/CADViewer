#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "ModelLoader.h"
#include "BezierSurface.h"

// 着色器工具函数
static unsigned int CompileShader(unsigned int type, const std::string &source)
{
    unsigned int id = glCreateShader(type);
    const char *src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    int success;
    glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(id, 512, nullptr, info);
        std::cerr << "Shader compile error (" << (type == GL_VERTEX_SHADER ? "vert" : "frag")
                  << "):\n" << info << "\n";
    }
    return id;
}

static unsigned int CreateShaderProgram(const std::string &vertPath,
                                        const std::string &fragPath)
{
    // 读取顶点着色器
    std::ifstream vf(vertPath);
    std::stringstream vss;
    vss << vf.rdbuf();
    std::string vertSrc = vss.str();

    // 读取片元着色器
    std::ifstream ff(fragPath);
    std::stringstream fss;
    fss << ff.rdbuf();
    std::string fragSrc = fss.str();

    unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertSrc);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragSrc);

    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(program, 512, nullptr, info);
        std::cerr << "Shader link error:\n" << info << "\n";
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// 轨迹球相机（NX 风格轨迹球旋转）
// 存储相机的局部基向量并逐步旋转
struct Camera
{
    glm::vec3 target = glm::vec3(0.0f);
    float radius = 8.0f;

    // 世界空间中的相机局部基向量：
    //  fwd = 从 target 指向相机的单位向量
    //  right = 指向相机右侧的单位向量
    //  up = 指向相机上方的单位向量
    glm::vec3 fwd, right, up;

    Camera()
    {
        // 初始视角：方位角 45°，仰角 30°
        float phi = glm::radians(45.0f);
        float theta = glm::radians(30.0f);

        float x = cosf(theta) * sinf(phi);
        float y = sinf(theta);
        float z = cosf(theta) * cosf(phi);

        fwd = glm::normalize(glm::vec3(x, y, z));
        right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        up = glm::normalize(glm::cross(right, fwd));
    }

    glm::vec3 eyePos() const { return target + fwd * radius; }

    glm::mat4 viewMatrix() const
    {
        return glm::lookAt(eyePos(), target, up);
    }

    // 基于像素空间鼠标增量的轨迹球旋转
    void arcballRotate(float dx, float dy, float screenW, float /*screenH*/)
    {
        if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return;

        // 全屏宽度拖动 ≈ 360°
        float speed = glm::pi<float>() / glm::max(screenW, 1.0f);

        // 构建增量旋转矩阵
        glm::mat3 inc(1.0f);

        // 先垂直：绕相机右轴旋转
        if (fabsf(dy) >= 0.001f) {
            inc = glm::mat3(glm::rotate(glm::mat4(1.0f), -dy * speed, right));
        }

        // 水平：绕世界 Y 轴旋转
        if (fabsf(dx) >= 0.001f) {
            glm::mat3 rotY = glm::mat3(glm::rotate(glm::mat4(1.0f), -dx * speed,
                                          glm::vec3(0.0f, 1.0f, 0.0f)));
            inc = rotY * inc;
        }

        // 应用到基向量
        fwd = inc * fwd;
        right = inc * right;
        up = inc * up;
    }

    // 信息面板使用的导出角度 
    float getPhi()   const { return atan2f(fwd.x, fwd.z); }
    float getTheta() const { return asinf(glm::clamp(fwd.y, -1.0f, 1.0f)); }
};

// 通过 ImGui IO 实现轨道相机（不会覆盖 ImGui 回调）
// 每帧从 ImGui::GetIO() 读取鼠标/滚轮数据，仅在光标位于 3D
// 视口内时才操作（即不在面板上）。这样可以保持 ImGui 的按钮、滑块和滚动功能正常工作。

// 渲染循环、UI、FFD、交互
// 入口点
int main()
{
    // GLFW 初始化
    if (!glfwInit()) {
        std::cerr << "Failed to initialise GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow *window = glfwCreateWindow(1280, 720,
        "macOS CAD 查看器与曲面编辑器", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // 垂直同步

    // GLAD 初始化
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialise GLAD\n";
        return -1;
    }
    std::cout << "OpenGL " << glGetString(GL_VERSION) << "\n";

    // OpenGL 状态设置
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ImGui 初始化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    // 加载中文字体（宋体）
    io.Fonts->AddFontFromFileTTF(
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        18.0f, nullptr,
        io.Fonts->GetGlyphRangesChineseFull());

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // 着色器程序
    // 路径相对于 build/ 目录；根据需要调整。
    std::string shaderDir =
#ifdef PROJECT_SOURCE_DIR
        std::string(PROJECT_SOURCE_DIR) + "/shaders/";
#else
        "../shaders/";
#endif
    unsigned int shader = CreateShaderProgram(shaderDir + "core_shader.vert",
                                              shaderDir + "core_shader.frag");

    // 加载 STL 模型
    std::string assetDir =
#ifdef PROJECT_SOURCE_DIR
        std::string(PROJECT_SOURCE_DIR) + "/assets/";
#else
        "../assets/";
#endif

    Model model;
    bool modelLoaded = model.load(assetDir + "model.stl");
    if (!modelLoaded) {
        std::cerr << "警告：无法加载 model.stl — 仅显示贝塞尔曲面。\n";
    }

    // 基于模型包围盒自动缩放
    float    autoScale  = 1.0f;
    glm::vec3 autoCenter(0.0f);
    if (modelLoaded) {
        float extent = model.maxExtent();
        if (extent > 0.001f) {
            autoScale = 34.0f / extent;  // 归一化到约 25 个世界单位
        }
        autoCenter = model.center();
        std::cout << "Model bounds: " << model.size().x << " x "
                  << model.size().y << " x " << model.size().z
                  << "  |  auto-scale: " << autoScale << "\n";
    }

    // 贝塞尔曲面缓冲区
    InitDefaultControlPoints();

    unsigned int surfaceVAO = 0, surfaceVBO = 0, surfaceEBO = 0;
    unsigned int surfaceIndexCount = 0;

    glGenVertexArrays(1, &surfaceVAO);
    glGenBuffers(1, &surfaceVBO);
    glGenBuffers(1, &surfaceEBO);

    const int bezierResolution = 32;
    UpdateSurfaceBuffers(surfaceVAO, surfaceVBO, surfaceEBO,
                         bezierResolution, surfaceIndexCount);

    // FFD：快照参考控制点
    // 这些是"静止"位置；位移 = 当前 − 参考
    glm::vec3 refControlPoints[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            refControlPoints[i][j] = controlPoints[i][j];

    // 相机
    Camera camera;

    // UI 状态 
    glm::vec3 modelColor(1.0f, 0.6f, 0.0f);     // 暖橙色
    float alphaValue = 0.9f;
    bool useTexture = false;
    bool wireframe = false;
    float userScale = 1.0f;
    bool showSurface = true;    // 切换贝塞尔曲面显示
    glm::vec3 lightPos = glm::vec3(5.0f, 8.0f, 5.0f);
    glm::vec3 lightColor = glm::vec3(1.0f, 1.0f, 1.0f);

    // 主循环
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // ImGui NewFrame（必须在访问 io.MouseWheel 之前调用）
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 通过 ImGui IO 轨道/缩放（此时 io.MouseWheel 已有效）
        ImGuiIO &io = ImGui::GetIO();

        // 左键拖拽：轨迹球旋转
        if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            camera.arcballRotate(io.MouseDelta.x, io.MouseDelta.y,
                                 io.DisplaySize.x, io.DisplaySize.y);
        }

        // 中键拖拽：轨迹球旋转（NX 风格 360° 翻滚）
        if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            camera.arcballRotate(io.MouseDelta.x, io.MouseDelta.y,
                                 io.DisplaySize.x, io.DisplaySize.y);
        }

        // 滚轮缩放 — 仅在未悬停在 ImGui 窗口上时生效
        if (!io.WantCaptureMouse && fabs(io.MouseWheel) > 0.001f)
        {
            camera.radius -= io.MouseWheel * 0.8f;
            if (camera.radius < 1.0f)  camera.radius = 1.0f;
            if (camera.radius > 50.0f) camera.radius = 50.0f;
        }

        // 渲染
        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shader);

        // 共享 uniform
        glUniform3fv(glGetUniformLocation(shader, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(shader, "lightColor"), 1, glm::value_ptr(lightColor));
        glUniform1f( glGetUniformLocation(shader, "alpha"), alphaValue);
        glUniform1i( glGetUniformLocation(shader, "useTexture"), useTexture ? 1 : 0);

        glm::mat4 view = camera.viewMatrix();
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f), 1280.0f / 720.0f, 0.1f, 100.0f);

        glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        // 绘制模型
        if (modelLoaded) {
            if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glUniform3fv(glGetUniformLocation(shader, "objectColor"), 1, glm::value_ptr(modelColor));
            glm::mat4 modelMat = glm::mat4(1.0f);
            modelMat = glm::translate(modelMat, -autoCenter);     // 居中到原点
            modelMat = glm::scale(modelMat, glm::vec3(autoScale * userScale));
            glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(modelMat));
            model.draw();
            if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // 绘制贝塞尔曲面
        if (showSurface)
        {
            glm::vec3 surfColor(0.3f, 0.6f, 0.9f);
            glUniform3fv(glGetUniformLocation(shader, "objectColor"), 1, glm::value_ptr(surfColor));

            // 将贝塞尔曲面放置在模型中心，并进行非均匀缩放
            // 以匹配模型的世界空间包围盒。
            // 控制点网格在 X/Z 方向跨度约 3 个单位，Y 方向约 2 个单位。
            glm::mat4 surfModel = glm::mat4(1.0f);
            if (modelLoaded) {
                glm::vec3 ws = model.size() * autoScale;    // 世界空间尺寸
                float sx = ws.x / 3.0f;
                float sy = ws.y / 2.0f;
                float sz = ws.z / 3.0f;
                surfModel = glm::scale(surfModel, glm::vec3(sx, sy, sz));
            } else {
                surfModel = glm::scale(surfModel, glm::vec3(3.0f));
            }
            glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(surfModel));

            glBindVertexArray(surfaceVAO);
            glDrawElements(GL_TRIANGLES, (GLsizei)surfaceIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }

        glUseProgram(0);

        // Command+点击 → 在模型表面上设置轨道枢轴点
        if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && (io.KeyCtrl || io.KeySuper))
        {
            int winW = (int)io.DisplaySize.x;
            int winH = (int)io.DisplaySize.y;
            int mx = (int)io.MousePos.x;
            int my = winH - (int)io.MousePos.y - 1;   // GL 原点在左下角

            if (mx >= 0 && mx < winW && my >= 0 && my < winH) {
                glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                                  (float)winW / (float)winH,
                                                  0.1f, 100.0f);
                glm::vec4 vp(0, 0, winW, winH);

                // 读取点击处的几何深度；如果深度无效则回退到当前枢轴深度
                float depth = 1.0f;
                glReadPixels(mx, my, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
                if (depth >= 1.0f) {
                    glm::vec3 pivotScr = glm::project(camera.target,
                                                      camera.viewMatrix(),
                                                      proj, vp);
                    depth = pivotScr.z;
                }

                glm::vec3 worldPos = glm::unProject(
                    glm::vec3((float)mx, (float)my, depth),
                    camera.viewMatrix(), proj, vp);
                camera.target = worldPos;
            }
        }

        // ImGui 界面

        // 面板 1: 模型与渲染
        ImGui::SetNextWindowSize(ImVec2(380, 230), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("模型与渲染");
        if (modelLoaded) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "STL 已加载");
            ImGui::SameLine();
            ImGui::TextDisabled("  %d 三角形  |  %.0f x %.0f x %.0f",
                model.triangleCount(),
                model.size().x, model.size().y, model.size().z);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "STL 未找到");
        }
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::ColorEdit3("颜色", glm::value_ptr(modelColor));
        ImGui::SliderFloat("透明度", &alphaValue, 0.0f, 1.0f);
        ImGui::SliderFloat("缩放", &userScale, 0.1f, 5.0f, "%.2f");
        ImGui::Spacing();
        ImGui::Checkbox("线框模式", &wireframe);
        ImGui::SameLine();
        ImGui::Checkbox("使用纹理", &useTexture);
        ImGui::End();

        // 面板 2: 光照
        ImGui::SetNextWindowSize(ImVec2(380, 170), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 250), ImGuiCond_FirstUseEver);
        ImGui::Begin("光照");
        ImGui::SliderFloat3("光源位置", glm::value_ptr(lightPos), -10.0f, 10.0f);
        ImGui::Spacing();
        ImGui::ColorEdit3("光源颜色", glm::value_ptr(lightColor));
        ImGui::End();

        // 面板 3: 贝塞尔曲面编辑器
        ImGui::SetNextWindowSize(ImVec2(620, 320), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 430), ImGuiCond_FirstUseEver);
        ImGui::Begin("贝塞尔曲面编辑器");

        // 工具栏
        bool cpChanged = false;

        if (ImGui::Button("重置穹顶")) {
            InitDefaultControlPoints();
            UpdateSurfaceBuffers(surfaceVAO, surfaceVBO, surfaceEBO,
                                 bezierResolution, surfaceIndexCount);
            cpChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("展平")) {
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    controlPoints[i][j].y = 0.0f;
            UpdateSurfaceBuffers(surfaceVAO, surfaceVBO, surfaceEBO,
                                 bezierResolution, surfaceIndexCount);
            cpChanged = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("显示曲面", &showSurface);

        ImGui::SameLine();
        ImGui::TextDisabled("  拖动控制点以重塑曲面");

        ImGui::Separator();
        ImGui::Spacing();

        // 4×4 控制点表格
        if (ImGui::BeginTable("CPGrid", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
        {
            for (int i = 0; i < 4; ++i) {
                ImGui::TableNextRow();
                for (int j = 0; j < 4; ++j) {
                    ImGui::TableSetColumnIndex(j);
                    ImGui::PushID(i * 4 + j);

                    char header[16];
                    snprintf(header, sizeof(header), "控制点 [%d,%d]", i, j);
                    ImGui::TextUnformatted(header);
                    ImGui::Separator();

                    glm::vec3 &cp = controlPoints[i][j];
                    ImGui::SetNextItemWidth(-FLT_MIN);  // 填充单元格
                    if (ImGui::DragFloat("X", &cp.x, 0.05f, -5.0f, 5.0f, "%.2f"))
                        cpChanged = true;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::DragFloat("Y", &cp.y, 0.05f, -5.0f, 5.0f, "%.2f"))
                        cpChanged = true;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::DragFloat("Z", &cp.z, 0.05f, -5.0f, 5.0f, "%.2f"))
                        cpChanged = true;

                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }

        if (cpChanged) {
            UpdateSurfaceBuffers(surfaceVAO, surfaceVBO, surfaceEBO,
                                 bezierResolution, surfaceIndexCount);

            // FFD：通过贝塞尔控制点增量变形 STL 模型
            if (modelLoaded && model.totalVertexCount() > 0) {
                std::vector<glm::vec3> newPos(model.totalVertexCount());
                glm::vec3 ms = model.size();
                float sx = ms.x * autoScale / 3.0f;
                float sy = ms.y * autoScale / 2.0f;
                float sz = ms.z * autoScale / 3.0f;
                float invAS = 1.0f / (autoScale * userScale);

                const auto &orig = model.originalPositions();

                for (unsigned int vi = 0; vi < model.totalVertexCount(); ++vi)
                {
                    // 顶点 → 世界空间
                    glm::vec3 vw = (orig[vi] - autoCenter) * autoScale * userScale;

                    // 贝塞尔曲面片域内的参数 (u, v)
                    float u = glm::clamp(vw.x / (ms.x * autoScale) + 0.5f, 0.0f, 1.0f);
                    float v = glm::clamp(vw.z / (ms.z * autoScale) + 0.5f, 0.0f, 1.0f);

                    // 在 (u, v) 处评估当前曲面和参考曲面
                    auto evalCP = [](const glm::vec3 cp[4][4], float u_, float v_) {
                        glm::vec3 r(0.0f);
                        for (int i = 0; i < 4; ++i) {
                            float bu = Bernstein(i, u_);
                            for (int j = 0; j < 4; ++j)
                                r += cp[i][j] * (bu * Bernstein(j, v_));
                        }
                        return r;
                    };
                    glm::vec3 cur = evalCP(controlPoints, u, v);
                    glm::vec3 ref = evalCP(refControlPoints, u, v);

                    // 位移：曲面局部 → 世界 → 模型局部
                    glm::vec3 dLocal = cur - ref;
                    glm::vec3 dWorld(dLocal.x * sx, dLocal.y * sy, dLocal.z * sz);
                    newPos[vi] = orig[vi] + dWorld * invAS;
                }
                model.applyDeformation(newPos);
            }
        }
        ImGui::End();

        // 面板 4: 信息
        ImGui::SetNextWindowSize(ImVec2(250, 220), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(640, 250), ImGuiCond_FirstUseEver);
        ImGui::Begin("信息");
        ImGui::Text("相机");
        ImGui::BulletText("方位角  %.0f°", glm::degrees(camera.getPhi()));
        ImGui::BulletText("仰角    %.0f°", glm::degrees(camera.getTheta()));
        ImGui::BulletText("距离    %.1f",   camera.radius);
        ImGui::Separator();
        ImGui::Text("枢轴");
        ImGui::BulletText("X  %.2f", camera.target.x);
        ImGui::BulletText("Y  %.2f", camera.target.y);
        ImGui::BulletText("Z  %.2f", camera.target.z);
        ImGui::TextDisabled("Ctrl+点击 设枢轴");
        ImGui::Separator();
        ImGui::Text("曲面");
        ImGui::BulletText("网格    %d x %d", bezierResolution, bezierResolution);
        ImGui::BulletText("三角形  %u",      surfaceIndexCount / 3);
        ImGui::End();

        // 完成帧渲染
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // 清理资源
    model.destroy();

    if (surfaceVAO) glDeleteVertexArrays(1, &surfaceVAO);
    if (surfaceVBO) glDeleteBuffers(1, &surfaceVBO);
    if (surfaceEBO) glDeleteBuffers(1, &surfaceEBO);

    glDeleteProgram(shader);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Clean exit.\n";
    return 0;
}
