#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

uniform vec3  objectColor;
uniform vec3  lightPos;
uniform vec3  lightColor;
uniform float alpha;           // 主透明度
uniform bool  useTexture;

// 片元着色器
void main()
{
    // 环境光
    float ambientStrength = 0.2;
    vec3  ambient = ambientStrength * lightColor;

    // 漫反射
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3  diffuse = diff * lightColor;

    // 镜面反射（Blinn-Phong 风格，使用视角相关的半向量）
    float specularStrength = 0.5;
    vec3  viewDir = normalize(-FragPos);
    vec3  reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3  specular = specularStrength * spec * lightColor;

    // 基础颜色：程序化棋盘格或均匀颜色
    vec4 baseColor;
    if (useTexture) {
        // 世界空间 3D 棋盘格 — 适用于任何模型（无需 UV）
        float   scale = 0.6;
        ivec3   grid = ivec3(floor(FragPos * scale));
        float   pattern = float((grid.x + grid.y + grid.z) & 1);
        vec3    checker = mix(objectColor * 0.4, objectColor * 1.5, pattern);
        baseColor = vec4(checker, 1.0);
    } else {
        baseColor = vec4(objectColor, 1.0);
    }

    vec3 result = (ambient + diffuse + specular) * baseColor.rgb;

    // 输出带混合 alpha 以支持半透明效果
    FragColor = vec4(result, baseColor.a * alpha);
}
