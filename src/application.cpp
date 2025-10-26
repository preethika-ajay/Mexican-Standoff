//#include "Image.h"
#include "mesh.h"
#include "texture.h"
// Always include window first (because it includes glfw, which includes GL which needs to be included AFTER glad).
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>          // glad before glfw
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()
#include <framework/shader.h>
#include <framework/window.h>
#include <functional>
#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

// ---- Multi-light globals ----
int selectedLightIndex = 0;

struct PointLight {
    glm::vec3 position { 2.0f, 3.0f, 2.0f };
    glm::vec3 color    { 1.0f, 1.0f, 1.0f };
    float     intensity{ 1.0f };
    float     pointSize{ 18.0f }; // for the on-screen orb only
};

class Application {
public:
    Application()
        : m_window("Final Project", glm::ivec2(1024, 1024), OpenGLVersion::GL41)
        , m_texture(RESOURCE_ROOT "resources/checkerboard.png")
        , m_cameraPosition(0.0f, 1.5f, 5.0f)
        , m_cameraTarget(0.0f, 0.5f, 0.0f)
        , m_cameraUp(0.0f, 1.0f, 0.0f)
    {
        // Initialize camera front/yaw/pitch from initial target/position
        m_cameraFront = glm::normalize(m_cameraTarget - m_cameraPosition);
        m_yaw   = glm::degrees(std::atan2(m_cameraFront.z, m_cameraFront.x));
        m_pitch = glm::degrees(std::asin(glm::clamp(m_cameraFront.y, -1.0f, 1.0f)));

        // Input callbacks
        m_window.registerKeyCallback([this](int key, int /*scancode*/, int action, int mods) {
            if (action == GLFW_PRESS)   onKeyPressed(key, mods);
            else if (action == GLFW_RELEASE) onKeyReleased(key, mods);
        });
        m_window.registerMouseMoveCallback(std::bind(&Application::onMouseMove, this, std::placeholders::_1));
        m_window.registerMouseButtonCallback([this](int button, int action, int mods) {
            if (action == GLFW_PRESS)   onMouseClicked(button, mods);
            else if (action == GLFW_RELEASE) onMouseReleased(button, mods);
        });

        // Keep projection & viewport in sync with window size/aspect
        m_window.registerWindowResizeCallback([this](const glm::ivec2& size) {
            glViewport(0, 0, size.x, size.y);
            m_projectionMatrix = glm::perspective(glm::radians(80.0f),
                                                  float(size.x) / float(size.y),
                                                  0.1f, 30.0f);
        });

        // Load models
        m_meshesA = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/gunslinger_cylindrical_v3_caps.obj");
        m_meshesB = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/gunslinger_cylindrical_v3_caps_mirrorX.obj");
        ground    = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/ground_plane.obj");


        try {
            // Default shader (lit + shadows)
            ShaderBuilder defaultBuilder;
            defaultBuilder.addStage(GL_VERTEX_SHADER,   RESOURCE_ROOT "shaders/shader_vert.glsl");
            defaultBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shader_frag.glsl");
            m_defaultShader = defaultBuilder.build();

            // Depth-only shader for shadow maps
            ShaderBuilder shadowBuilder;
            shadowBuilder.addStage(GL_VERTEX_SHADER,   RESOURCE_ROOT "shaders/shadow_vert.glsl");
            shadowBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shadow_frag.glsl");
            m_shadowShader = shadowBuilder.build();

            // Light orb (point sprite)
            ShaderBuilder lp;
            lp.addStage(GL_VERTEX_SHADER,   RESOURCE_ROOT "shaders/light_point_vert.glsl");
            lp.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/light_point_frag.glsl");
            m_lightPointShader = lp.build();

        } catch (const ShaderLoadingException& e) {
            std::cerr << e.what() << std::endl;
        }

        // Basic GL state
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glGenVertexArrays(1, &m_dummyVAO); // core profile needs a VAO bound to draw

        // Time
        m_lastTime = glfwGetTime();

        // One default light
        m_lights.push_back(PointLight{ glm::vec3(2.0f, 3.0f, 2.0f), glm::vec3(1.0f), 1.0f, 18.0f });
        selectedLightIndex = 0;

        initShadowResources();
    }

    void update()
    {
        int dummyInteger = 0;
        while (!m_window.shouldClose()) {
            // Input/update
            m_window.updateInput();

            const double now = glfwGetTime();
            float dt = static_cast<float>(now - m_lastTime);
            m_lastTime = now;
            dt = glm::clamp(dt, 0.0f, 0.05f);

            handleContinuousKeyboard(dt);

            m_cameraTarget = m_cameraPosition + m_cameraFront;
            m_viewMatrix   = glm::lookAt(m_cameraPosition, m_cameraTarget, m_cameraUp);

            // UI
            ImGui::Begin("Window");
            ImGui::InputInt("This is an integer input", &dummyInteger);
            ImGui::Text("Value is: %i", dummyInteger);
            ImGui::Checkbox("Use material if no texture", &m_useMaterial);

            ImGui::Separator();
            ImGui::Text("Camera (RMB look, WASD/Q/E move)");
            ImGui::DragFloat3("Camera Position", glm::value_ptr(m_cameraPosition), 0.1f);
            ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_yaw, m_pitch);
            ImGui::SliderFloat("Mouse Sensitivity", &m_mouseSensitivity, 0.01f, 0.5f);
            ImGui::SliderFloat("Move Speed (m/s)",  &m_moveSpeed,        0.05f, 10.0f);

            ImGui::Separator();
            ImGui::Text("Model Positioning");
            ImGui::DragFloat("Distance Between Models", &m_modelDistance, 0.1f, 0.0f, 10.0f);


            ImGui::Separator();
            ImGui::Text("Material");
            ImGui::SliderFloat("kd (diffuse)",  &m_kd, 0.0f, 2.0f);
            ImGui::SliderFloat("ks (specular)", &m_ks, 0.0f, 2.0f);


            // Lights GUI
            ImGui::Separator();
            ImGui::Text("Lights");
            if (ImGui::BeginListBox("Light List", ImVec2(-FLT_MIN, 120))) {
                for (int i = 0; i < (int)m_lights.size(); ++i) {
                    const bool isSelected = (selectedLightIndex == i);
                    std::string label = "Light " + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), isSelected)) selectedLightIndex = i;
                }
                ImGui::EndListBox();
            }
            if (!m_lights.empty() && selectedLightIndex >= 0 && selectedLightIndex < (int)m_lights.size()) {
                auto& L = m_lights[selectedLightIndex];
                ImGui::DragFloat3("Position", glm::value_ptr(L.position), 0.1f);
                ImGui::ColorEdit3("Color", glm::value_ptr(L.color));
                ImGui::SliderFloat("Intensity", &L.intensity, 0.0f, 5.0f);
                ImGui::SliderFloat("Orb Size (px)", &L.pointSize, 4.0f, 64.0f);
            }
            ImGui::TextDisabled("Shift+L = add light at camera,  L = move selected to camera");

            // Shadows GUI
            ImGui::Separator();
            ImGui::Text("Shadows");
            ImGui::SliderInt("Shadow Map Size", &m_shadowMapSize, 256, 4096);
            ImGui::SliderFloat("PCF texel scale", &m_pcfTexelScale, 0.5f, 3.0f);
            ImGui::SliderFloat("Shadow bias", &m_shadowBias, 0.0005f, 0.02f);
            ImGui::SliderFloat("Parallel-skip threshold", &m_shadowMinNdotL, 0.0f, 0.2f);
            ImGui::End();

            // Resize shadow maps if needed
            resizeShadowResourcesIfNeeded();

            // PASS 1: render shadow maps (full body casts)
            renderShadowMaps();

            // PASS 2: render scene
            glViewport(0, 0, m_window.getWindowSize().x, m_window.getWindowSize().y);
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            renderLightPoints();

            // Ground (receives shadows)
            renderModel(ground, glm::mat4(1.0f), /*isGround*/true);

            // Characters (occluders only, not receivers)
            glm::mat4 modelMatrixA = glm::translate(glm::mat4(1.0f), glm::vec3(-m_modelDistance * 0.5f, 0.0f, 0.0f));
            glm::mat4 modelMatrixB = glm::translate(glm::mat4(1.0f), glm::vec3( m_modelDistance * 0.5f, 0.0f, 0.0f));
            renderModel(m_meshesA, modelMatrixA, /*isGround*/false);
            renderModel(m_meshesB, modelMatrixB, /*isGround*/false);

            m_window.swapBuffers();
        }
    }

private:
    // ---------- Shadow resources ----------

    static constexpr int MAX_LIGHTS = 32;
    static constexpr int MAX_SHADOW_LIGHTS = 16;

    GLuint m_shadowFBO   [MAX_SHADOW_LIGHTS] = {0};
    GLuint m_shadowDepth [MAX_SHADOW_LIGHTS] = {0};
    glm::mat4 m_lightViewProj[MAX_SHADOW_LIGHTS];
    int   m_shadowMapSize  = 1024;
    float m_pcfTexelScale  = 1.0f;
    float m_shadowBias     = 0.0025f;
    float m_shadowMinNdotL = 0.08f; // NEW: parallel-skip threshold for ground shadows

    void initShadowResources()
    {
        for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
            glGenFramebuffers(1, &m_shadowFBO[i]);
            glGenTextures(1, &m_shadowDepth[i]);

            glBindTexture(GL_TEXTURE_2D, m_shadowDepth[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_shadowMapSize, m_shadowMapSize, 0,
                         GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            // Use linear filtering to soften PCF further
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowDepth[i], 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "Shadow FBO " << i << " incomplete! Status: 0x" << std::hex << status << std::dec << "\n";
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void resizeShadowResourcesIfNeeded()
    {
        static int prevSize = m_shadowMapSize;
        if (prevSize == m_shadowMapSize) return;
        prevSize = m_shadowMapSize;

        for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
            glBindTexture(GL_TEXTURE_2D, m_shadowDepth[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_shadowMapSize, m_shadowMapSize, 0,
                         GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Perspective shadow camera from the light toward the scene.
    glm::mat4 buildLightViewProjPerspective(const glm::vec3& lightPos) const
    {
        glm::vec3 sceneCenter(0.0f, 0.0f, 0.0f);                // center on ground
        float radius = 2.5f + 0.5f * m_modelDistance;
        radius += std::abs(lightPos.y) * 1.5f;                  // include long shadows

        glm::vec3 dir = sceneCenter - lightPos;
        glm::vec3 up = (std::abs(glm::dot(glm::normalize(dir), glm::vec3(0,1,0))) > 0.95f)
                       ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
        glm::mat4 V = glm::lookAt(lightPos, sceneCenter, up);

        float dist = glm::length(dir);
        float nearZ = std::max(0.10f, dist - radius);
        float farZ  =           dist + radius + 20.0f;          // more slack to avoid clipping
        float fov = 2.0f * std::atan(radius / std::max(0.1f, dist));
        fov = glm::clamp(fov, glm::radians(30.0f), glm::radians(120.0f));

        glm::mat4 P = glm::perspective(fov, 1.0f, nearZ, farZ);
        return P * V;
    }

    void renderShadowMaps()
    {
        int shadowLights = std::min((int)m_lights.size(), MAX_SHADOW_LIGHTS);

        glEnable(GL_DEPTH_TEST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // depth-only
        m_shadowShader.bind();

        for (int i = 0; i < shadowLights; ++i) {
            m_lightViewProj[i] = buildLightViewProjPerspective(m_lights[i].position);

            glViewport(0, 0, m_shadowMapSize, m_shadowMapSize);
            glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO[i]);
            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT);

            auto drawDepth = [&](std::vector<GPUMesh>& meshes, const glm::mat4& modelMatrix) {
                const glm::mat4 mvp = m_lightViewProj[i] * modelMatrix;
                glUniformMatrix4fv(m_shadowShader.getUniformLocation("lightMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
                for (auto& m : meshes) m.draw(m_shadowShader);
            };

            glm::mat4 modelMatrixA = glm::translate(glm::mat4(1.0f), glm::vec3(-m_modelDistance * 0.5f, 0.0f, 0.0f));
            glm::mat4 modelMatrixB = glm::translate(glm::mat4(1.0f), glm::vec3( m_modelDistance * 0.5f, 0.0f, 0.0f));

            drawDepth(m_meshesA, modelMatrixA);
            drawDepth(m_meshesB, modelMatrixB);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    // ---------- Camera + draw helpers ----------

    void updateFrontFromYawPitch()
    {
        float cy = std::cos(glm::radians(m_yaw));
        float sy = std::sin(glm::radians(m_yaw));
        float cp = std::cos(glm::radians(m_pitch));
        float sp = std::sin(glm::radians(m_pitch));
        glm::vec3 front(cy * cp, sp, sy * cp);
        m_cameraFront = glm::normalize(front);
    }

    void handleContinuousKeyboard(float dt)
    {
        const glm::vec3 forwardXZ = glm::normalize(glm::vec3(m_cameraFront.x, 0.0f, m_cameraFront.z));
        const glm::vec3 right     = glm::normalize(glm::cross(forwardXZ, m_cameraUp));
        const float step = m_moveSpeed * dt;

        if (m_keyW) m_cameraPosition += forwardXZ * step;
        if (m_keyS) m_cameraPosition -= forwardXZ * step;
        if (m_keyA) m_cameraPosition -= right     * step;
        if (m_keyD) m_cameraPosition += right     * step;
        if (m_keyE) m_cameraPosition += m_cameraUp * step; // up
        if (m_keyQ) m_cameraPosition -= m_cameraUp * step; // down
    }

    void renderLightPoints()
    {
        glm::mat4 viewProj = m_projectionMatrix * m_viewMatrix;
        m_lightPointShader.bind();
        glBindVertexArray(m_dummyVAO);
        for (const auto& L : m_lights) {
            glUniformMatrix4fv(m_lightPointShader.getUniformLocation("viewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
            glUniform3fv(m_lightPointShader.getUniformLocation("lightPos"), 1, glm::value_ptr(L.position));
            glUniform1f(m_lightPointShader.getUniformLocation("pointSize"), L.pointSize);
            glUniform3fv(m_lightPointShader.getUniformLocation("lightColor"), 1, glm::value_ptr(L.color));
            glDrawArrays(GL_POINTS, 0, 1);
        }
        glBindVertexArray(0);
    }

    void renderModel(std::vector<GPUMesh>& meshes, const glm::mat4& modelMatrix, bool isGround)
    {
        const glm::mat4 mvpMatrix         = m_projectionMatrix * m_viewMatrix * modelMatrix;
        const glm::mat3 normalModelMatrix = glm::inverseTranspose(glm::mat3(modelMatrix));

        // Lights
        int n = (int)std::min<size_t>(m_lights.size(), MAX_LIGHTS);
        glm::vec3 lp[MAX_LIGHTS], lc[MAX_LIGHTS];
        float li[MAX_LIGHTS];
        for (int i = 0; i < n; ++i) { lp[i]=m_lights[i].position; lc[i]=m_lights[i].color; li[i]=m_lights[i].intensity; }

        // Shadows
        int sN = std::min(n, MAX_SHADOW_LIGHTS);
        glm::mat4 lvp[MAX_SHADOW_LIGHTS];
        for (int i = 0; i < sN; ++i) lvp[i] = m_lightViewProj[i];

        // Bind shadow maps to texture units 1..N
        for (int i = 0; i < sN; ++i) { glActiveTexture(GL_TEXTURE1 + i); glBindTexture(GL_TEXTURE_2D, m_shadowDepth[i]); }
        glActiveTexture(GL_TEXTURE0);

        for (GPUMesh& mesh : meshes) {
            m_defaultShader.bind();

            glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"),         1, GL_FALSE, glm::value_ptr(mvpMatrix));
            glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"),       1, GL_FALSE, glm::value_ptr(modelMatrix));
            glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"), 1, GL_FALSE, glm::value_ptr(normalModelMatrix));

            glUniform1i (m_defaultShader.getUniformLocation("numLights"), n);
            if (n > 0) {
                glUniform3fv(m_defaultShader.getUniformLocation("lightPosition"),  n, glm::value_ptr(lp[0]));
                glUniform3fv(m_defaultShader.getUniformLocation("lightColor"),     n, glm::value_ptr(lc[0]));
                glUniform1fv(m_defaultShader.getUniformLocation("lightIntensity"), n, li);
            }
            glUniform3fv(m_defaultShader.getUniformLocation("viewPosition"), 1, glm::value_ptr(m_cameraPosition));

            glUniform1f(m_defaultShader.getUniformLocation("kd"), m_kd);
            glUniform1f(m_defaultShader.getUniformLocation("ks"), m_ks);

            glUniform1i(m_defaultShader.getUniformLocation("numShadowMaps"), sN);
            if (sN > 0) {
                for (int i = 0; i < sN; ++i) {
                    std::string name = "shadowMaps[" + std::to_string(i) + "]";
                    glUniform1i(m_defaultShader.getUniformLocation(name.c_str()), 1 + i);
                }
                glUniformMatrix4fv(m_defaultShader.getUniformLocation("lightViewProj"), sN, GL_FALSE, glm::value_ptr(lvp[0]));
                glUniform2f(m_defaultShader.getUniformLocation("shadowTexelSize"),
                            m_pcfTexelScale / float(m_shadowMapSize),
                            m_pcfTexelScale / float(m_shadowMapSize));
                glUniform1f(m_defaultShader.getUniformLocation("shadowBias"), m_shadowBias);
                glUniform1f(m_defaultShader.getUniformLocation("shadowMinNdotL"), m_shadowMinNdotL); // NEW
            }
            glUniform1i(m_defaultShader.getUniformLocation("isGround"), isGround ? 1 : 0);

            glm::vec3 matColor = glm::vec3(0.8f);
            glUniform3fv(m_defaultShader.getUniformLocation("materialColor"), 1, glm::value_ptr(matColor));

            if (mesh.hasTextureCoords()) {
                m_texture.bind(GL_TEXTURE0);
                glUniform1i(m_defaultShader.getUniformLocation("colorMap"),     0);
                glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_TRUE);
                glUniform1i(m_defaultShader.getUniformLocation("useMaterial"),  GL_FALSE);
            } else {
                glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_FALSE);
                glUniform1i(m_defaultShader.getUniformLocation("useMaterial"),  m_useMaterial);
            }

            mesh.draw(m_defaultShader);
        }

        // Unbind shadow textures
        for (int i = 0; i < sN; ++i) { glActiveTexture(GL_TEXTURE1 + i); glBindTexture(GL_TEXTURE_2D, 0); }
        glActiveTexture(GL_TEXTURE0);
    }

    // ---------- Events ----------

    void onKeyPressed(int key, int mods)
    {
        if (key == GLFW_KEY_W) m_keyW = true;
        if (key == GLFW_KEY_A) m_keyA = true;
        if (key == GLFW_KEY_S) m_keyS = true;
        if (key == GLFW_KEY_D) m_keyD = true;
        if (key == GLFW_KEY_E) m_keyE = true; // up
        if (key == GLFW_KEY_Q) m_keyQ = true; // down

        // Shift+L -> create a new light at the camera and select it
        if (key == GLFW_KEY_L && (mods & GLFW_MOD_SHIFT)) {
            PointLight pl;
            pl.position  = m_cameraPosition + m_cameraFront * 0.5f;
            pl.color     = glm::vec3(1.0f);
            pl.intensity = 1.0f;
            pl.pointSize = 18.0f;
            m_lights.push_back(pl);
            selectedLightIndex = (int)m_lights.size() - 1;
        }
        // Plain L -> move selected light to camera
        if (key == GLFW_KEY_L && !(mods & GLFW_MOD_SHIFT)) {
            if (!m_lights.empty() && selectedLightIndex >= 0 && selectedLightIndex < (int)m_lights.size())
                m_lights[selectedLightIndex].position = m_cameraPosition + m_cameraFront * 0.5f;
        }
    }

    void onKeyReleased(int key, int /*mods*/)
    {
        if (key == GLFW_KEY_W) m_keyW = false;
        if (key == GLFW_KEY_A) m_keyA = false;
        if (key == GLFW_KEY_S) m_keyS = false;
        if (key == GLFW_KEY_D) m_keyD = false;
        if (key == GLFW_KEY_E) m_keyE = false;
        if (key == GLFW_KEY_Q) m_keyQ = false;
    }

    void onMouseMove(const glm::dvec2& cursorPos)
    {
        if (!m_haveLastCursor) { m_lastCursor = cursorPos; m_haveLastCursor = true; }
        if (m_rotating) {
            glm::dvec2 d = cursorPos - m_lastCursor;
            m_yaw   += (float)d.x * m_mouseSensitivity * 10.0f;
            m_pitch -= (float)d.y * m_mouseSensitivity * 10.0f;
            m_pitch = glm::clamp(m_pitch, -89.0f, 89.0f);
            updateFrontFromYawPitch();
        }
        m_lastCursor = cursorPos;
    }

    void onMouseClicked(int button, int /*mods*/)  { if (button == GLFW_MOUSE_BUTTON_RIGHT) m_rotating = true; }
    void onMouseReleased(int button, int /*mods*/) { if (button == GLFW_MOUSE_BUTTON_RIGHT) m_rotating = false; }

private:
    Window m_window;

    // Shaders
    Shader m_defaultShader;
    Shader m_shadowShader;
    Shader m_lightPointShader;

    // Models
    std::vector<GPUMesh> m_meshesA, m_meshesB, ground;

    // Lights
    std::vector<PointLight> m_lights;

    Texture m_texture;
    bool m_useMaterial { true };

    // Camera
    glm::vec3 m_cameraPosition, m_cameraTarget, m_cameraUp;
    glm::vec3 m_cameraFront { 0.0f, 0.0f, -1.0f };
    float     m_yaw   { -90.0f };
    float     m_pitch {   0.0f };
    float     m_mouseSensitivity { 0.05f };
    float     m_moveSpeed { 2.0f };

    float m_kd { 1.0f };  // diffuse coefficient
    float m_ks { 0.25f }; // specular coefficient

    // Mouse-look state
    bool        m_rotating { false };
    bool        m_haveLastCursor { false };
    glm::dvec2  m_lastCursor { 0.0, 0.0 };

    // Key states
    bool m_keyW { false }, m_keyA { false }, m_keyS { false }, m_keyD { false };
    bool m_keyQ { false }, m_keyE { false };

    // Light orb rendering
    GLuint m_dummyVAO { 0 };

    // Model positioning
    float m_modelDistance { 2.0f };

    // Matrices
    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 30.0f);
    glm::mat4 m_viewMatrix       = glm::mat4(1.0f);

    // Timing
    double m_lastTime { 0.0 };
};

int main()
{
    Application app;
    app.update();
    return 0;
}
