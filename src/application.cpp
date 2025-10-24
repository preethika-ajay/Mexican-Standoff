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

class Application {
public:
    Application()
        : m_window("Final Project", glm::ivec2(1024, 1024), OpenGLVersion::GL41)
        , m_texture(RESOURCE_ROOT "resources/checkerboard.png")
        , m_cameraPosition(0.0f, 1.5f, 5.0f)
        , m_cameraTarget(0.0f, 0.5f, 0.0f)
        , m_cameraUp(0.0f, 1.0f, 0.0f)
        , m_lightPosition(2.0f, 3.0f, 2.0f)
        , m_lightColor(1.0f, 1.0f, 1.0f)
        , m_lightIntensity(1.0f)
    {
        // Initialize camera front/yaw/pitch from initial target/position
        m_cameraFront = glm::normalize(m_cameraTarget - m_cameraPosition);
        m_yaw   = glm::degrees(std::atan2(m_cameraFront.z, m_cameraFront.x));     // [-180,180]
        m_pitch = glm::degrees(std::asin(glm::clamp(m_cameraFront.y, -1.0f, 1.0f)));

        // Input callbacks
        m_window.registerKeyCallback([this](int key, int, int action, int mods) {
            if (action == GLFW_PRESS)
                onKeyPressed(key, mods);
            else if (action == GLFW_RELEASE)
                onKeyReleased(key, mods);
        });
        m_window.registerMouseMoveCallback(std::bind(&Application::onMouseMove, this, std::placeholders::_1));
        m_window.registerMouseButtonCallback([this](int button, int action, int mods) {
            if (action == GLFW_PRESS)
                onMouseClicked(button, mods);
            else if (action == GLFW_RELEASE)
                onMouseReleased(button, mods);
        });

        // Keep projection & viewport in sync with window size/aspect
        m_window.registerWindowResizeCallback([this](const glm::ivec2& size) {
            glViewport(0, 0, size.x, size.y);
            m_projectionMatrix = glm::perspective(glm::radians(80.0f),
                                                  float(size.x) / float(size.y),
                                                  0.1f, 30.0f);
        });

        // Load both gunslinger models with color
        m_meshesA = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/gunslinger_a_with_color_final.obj");
        m_meshesB = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/gunslinger_b_with_color_final.obj");

        try {
            // Default shader (lit)
            ShaderBuilder defaultBuilder;
            defaultBuilder.addStage(GL_VERTEX_SHADER,   RESOURCE_ROOT "shaders/shader_vert.glsl");
            defaultBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shader_frag.glsl");
            m_defaultShader = defaultBuilder.build();

            // Shadow shader program (compiled but unused here)
            ShaderBuilder shadowBuilder;
            shadowBuilder.addStage(GL_VERTEX_SHADER,   RESOURCE_ROOT "shaders/shadow_vert.glsl");
            shadowBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shadow_frag.glsl");
            m_shadowShader = shadowBuilder.build();

            // Light orb (point sprite) shader — separate, emissive
            ShaderBuilder lp;
            lp.addStage(GL_VERTEX_SHADER,   RESOURCE_ROOT "shaders/light_point_vert.glsl");
            lp.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/light_point_frag.glsl");
            m_lightPointShader = lp.build();

        } catch (const ShaderLoadingException& e) {
            std::cerr << e.what() << std::endl;
        }

        // Basic GL state
        glEnable(GL_DEPTH_TEST);
        // glEnable(GL_CULL_FACE); glCullFace(GL_BACK); // optional

        // Point sprite setup for the light orb
        glEnable(GL_PROGRAM_POINT_SIZE);
        glGenVertexArrays(1, &m_dummyVAO); // core profile needs a VAO bound to draw

        // Initialize time for smooth continuous movement
        m_lastTime = glfwGetTime();
    }

    void update()
    {
        int dummyInteger = 0; // Initialized to 0
        while (!m_window.shouldClose()) {
            // Per-frame
            m_window.updateInput();

            // Delta time (seconds)
            const double now = glfwGetTime();
            float dt = static_cast<float>(now - m_lastTime);
            m_lastTime = now;
            // Cap dt to avoid giant leaps if the app stalls
            dt = glm::clamp(dt, 0.0f, 0.05f); // max ~50ms/frame

            // Continuous WASD movement while keys are held
            handleContinuousKeyboard(dt);

            // Update target from front each frame
            m_cameraTarget = m_cameraPosition + m_cameraFront;

            // Update view matrix based on camera
            m_viewMatrix = glm::lookAt(m_cameraPosition, m_cameraTarget, m_cameraUp);

            // UI
            ImGui::Begin("Window");
            ImGui::InputInt("This is an integer input", &dummyInteger);
            ImGui::Text("Value is: %i", dummyInteger);
            ImGui::Checkbox("Use material if no texture", &m_useMaterial);

            // Camera controls
            ImGui::Separator();
            ImGui::Text("Camera Controls (Hold RMB to look; WASD/Space/Shift to move)");
            ImGui::DragFloat3("Camera Position", glm::value_ptr(m_cameraPosition), 0.1f);
            ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_yaw, m_pitch);
            ImGui::SliderFloat("Mouse Sensitivity", &m_mouseSensitivity, 0.01f, 0.5f);
            ImGui::SliderFloat("Move Speed (m/s)",  &m_moveSpeed,        0.05f, 10.0f);
            if (ImGui::Button("Reset Camera")) {
                m_cameraPosition = glm::vec3(0.0f, 1.0f, 3.0f);
                m_cameraFront    = glm::normalize(glm::vec3(0.0f, 0.5f, 0.0f) - m_cameraPosition);
                m_yaw   = glm::degrees(std::atan2(m_cameraFront.z, m_cameraFront.x));
                m_pitch = glm::degrees(std::asin(glm::clamp(m_cameraFront.y, -1.0f, 1.0f)));
            }

            // Model positioning controls
            ImGui::Separator();
            ImGui::Text("Model Positioning");
            ImGui::DragFloat("Distance Between Models", &m_modelDistance, 0.1f, 0.0f, 10.0f);

            // Light controls
            ImGui::Separator();
            ImGui::Text("Light Controls");
            ImGui::DragFloat3("Light Position", glm::value_ptr(m_lightPosition), 0.1f);
            ImGui::ColorEdit3("Light Color",     glm::value_ptr(m_lightColor));
            ImGui::SliderFloat("Light Intensity", &m_lightIntensity, 0.0f, 5.0f);
            ImGui::SliderFloat("Light Orb Size (px)", &m_lightPointSize, 4.0f, 64.0f);
            if (ImGui::Button("Reset Light")) {
                m_lightPosition  = glm::vec3(2.0f, 3.0f, 2.0f);
                m_lightColor     = glm::vec3(1.0f, 1.0f, 1.0f);
                m_lightIntensity = 1.0f;
                m_lightPointSize = 18.0f;
            }
            ImGui::End();

            // Clear
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // 1) Draw the light orb (point sprite billboard)
            renderLightPoint();

            // 2) Render first gunslinger (A) - positioned to the left
            glm::mat4 modelMatrixA = glm::translate(glm::mat4(1.0f),
                                                    glm::vec3(-m_modelDistance * 0.5f, 0.0f, 0.0f));
            renderModel(m_meshesA, modelMatrixA);

            // 3) Render second gunslinger (B) - positioned to the right
            glm::mat4 modelMatrixB = glm::translate(glm::mat4(1.0f),
                                                    glm::vec3( m_modelDistance * 0.5f, 0.0f, 0.0f));
            renderModel(m_meshesB, modelMatrixB);

            // Present
            m_window.swapBuffers();
        }
    }

private:
    // ---------- Camera helpers ----------

    void updateFrontFromYawPitch()
    {
        // Convert yaw/pitch (in degrees) to front vector
        float cy = std::cos(glm::radians(m_yaw));
        float sy = std::sin(glm::radians(m_yaw));
        float cp = std::cos(glm::radians(m_pitch));
        float sp = std::sin(glm::radians(m_pitch));

        glm::vec3 front;
        front.x = cy * cp;
        front.y = sp;
        front.z = sy * cp;

        m_cameraFront = glm::normalize(front);
    }

    void handleContinuousKeyboard(float dt)
    {
        // Move relative to camera orientation; called every frame with dt in seconds
        const glm::vec3 forwardXZ = glm::normalize(glm::vec3(m_cameraFront.x, 0.0f, m_cameraFront.z)); // horizontal forward
        const glm::vec3 right     = glm::normalize(glm::cross(forwardXZ, m_cameraUp));

        const float step = m_moveSpeed * dt; // meters per second -> per-frame

        if (m_keyW)       m_cameraPosition += forwardXZ * step;
        if (m_keyS)       m_cameraPosition -= forwardXZ * step;
        if (m_keyA)       m_cameraPosition -= right     * step;
        if (m_keyD)       m_cameraPosition += right     * step;
        if (m_keySpace)   m_cameraPosition += m_cameraUp * step;
        if (m_keyShift)   m_cameraPosition -= m_cameraUp * step;
    }

    // ---------- Drawing helpers ----------

    // Helper: draw one GL_POINT as a soft round orb at m_lightPosition
    void renderLightPoint()
    {
        glm::mat4 viewProj = m_projectionMatrix * m_viewMatrix;

        m_lightPointShader.bind();
        glUniformMatrix4fv(m_lightPointShader.getUniformLocation("viewProj"),
                           1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform3fv(m_lightPointShader.getUniformLocation("lightPos"),
                     1, glm::value_ptr(m_lightPosition));
        glUniform1f(m_lightPointShader.getUniformLocation("pointSize"), m_lightPointSize);
        glUniform3fv(m_lightPointShader.getUniformLocation("lightColor"),
                     1, glm::value_ptr(m_lightColor));

        glBindVertexArray(m_dummyVAO);
        glDrawArrays(GL_POINTS, 0, 1);
        glBindVertexArray(0);
    }

    // Helper to render a model with a given model matrix
    void renderModel(std::vector<GPUMesh>& meshes, const glm::mat4& modelMatrix)
    {
        const glm::mat4 mvpMatrix          = m_projectionMatrix * m_viewMatrix * modelMatrix;
        const glm::mat3 normalModelMatrix  = glm::inverseTranspose(glm::mat3(modelMatrix));

        for (GPUMesh& mesh : meshes) {
            m_defaultShader.bind();

            // Matrices
            glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"),
                               1, GL_FALSE, glm::value_ptr(mvpMatrix));
            glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"),
                               1, GL_FALSE, glm::value_ptr(modelMatrix));   // REQUIRED by shader_vert.glsl
            glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"),
                               1, GL_FALSE, glm::value_ptr(normalModelMatrix));

            // Lighting
            glUniform3fv(m_defaultShader.getUniformLocation("lightPosition"),  1, glm::value_ptr(m_lightPosition));
            glUniform3fv(m_defaultShader.getUniformLocation("lightColor"),     1, glm::value_ptr(m_lightColor));
            glUniform1f (m_defaultShader.getUniformLocation("lightIntensity"),    m_lightIntensity);
            glUniform3fv(m_defaultShader.getUniformLocation("viewPosition"),   1, glm::value_ptr(m_cameraPosition));

            // Material color (fallback; GPUMesh has no public 'material' here)
            glm::vec3 matColor = glm::vec3(0.8f); // default gray
            matColor = glm::clamp(matColor, 0.0f, 1.0f);
            glUniform3fv(m_defaultShader.getUniformLocation("materialColor"),
                         1, glm::value_ptr(matColor));

            // Texture / material switches
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
    }

    // ---------- Event handlers ----------

    // Now key callbacks ONLY update the held-state booleans — no movement here.
    void onKeyPressed(int key, int /*mods*/)
    {
        if (key == GLFW_KEY_W)                     m_keyW     = true;
        if (key == GLFW_KEY_A)                     m_keyA     = true;
        if (key == GLFW_KEY_S)                     m_keyS     = true;
        if (key == GLFW_KEY_D)                     m_keyD     = true;
        if (key == GLFW_KEY_SPACE)                 m_keySpace = true;
        if (key == GLFW_KEY_LEFT_SHIFT ||
            key == GLFW_KEY_RIGHT_SHIFT)          m_keyShift = true;

        if (key == GLFW_KEY_L) {
            // Place the light at/just ahead of the camera so it's visible
            const float ahead = 0.5f;
            m_lightPosition = m_cameraPosition + m_cameraFront * ahead;
        }
    }

    void onKeyReleased(int key, int /*mods*/)
    {
        if (key == GLFW_KEY_W)                     m_keyW     = false;
        if (key == GLFW_KEY_A)                     m_keyA     = false;
        if (key == GLFW_KEY_S)                     m_keyS     = false;
        if (key == GLFW_KEY_D)                     m_keyD     = false;
        if (key == GLFW_KEY_SPACE)                 m_keySpace = false;
        if (key == GLFW_KEY_LEFT_SHIFT ||
            key == GLFW_KEY_RIGHT_SHIFT)          m_keyShift = false;
    }

    void onMouseMove(const glm::dvec2& cursorPos)
    {
        if (!m_haveLastCursor) {
            m_lastCursor = cursorPos;
            m_haveLastCursor = true;
        }

        if (m_rotating) {
            // Mouse look (RMB held): update yaw/pitch by cursor delta
            glm::dvec2 delta = cursorPos - m_lastCursor;
            m_yaw   += static_cast<float>(delta.x) * m_mouseSensitivity * 10.0f; // scale factor
            m_pitch -= static_cast<float>(delta.y) * m_mouseSensitivity * 10.0f; // invert Y

            // Clamp pitch to avoid gimbal lock
            m_pitch = glm::clamp(m_pitch, -89.0f, 89.0f);

            updateFrontFromYawPitch();
        }

        m_lastCursor = cursorPos;
    }

    void onMouseClicked(int button, int /*mods*/)
    {
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            m_rotating = true; // hold RMB to rotate
        }
    }

    void onMouseReleased(int button, int /*mods*/)
    {
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            m_rotating = false;
        }
    }

private:
    Window m_window;

    // Shaders
    Shader m_defaultShader;
    Shader m_shadowShader;
    Shader m_lightPointShader;   // emissive point-orb

    // Two sets of meshes for both gunslinger models with color
    std::vector<GPUMesh> m_meshesA;
    std::vector<GPUMesh> m_meshesB;

    Texture m_texture;
    bool m_useMaterial { true };

    // Camera
    glm::vec3 m_cameraPosition;
    glm::vec3 m_cameraTarget;
    glm::vec3 m_cameraUp;
    glm::vec3 m_cameraFront { 0.0f, 0.0f, -1.0f };
    float     m_yaw   { -90.0f }; // degrees
    float     m_pitch {   0.0f }; // degrees
    float     m_mouseSensitivity { 0.05f };
    float     m_moveSpeed { 2.0f };   // meters per second

    // Mouse-look state
    bool        m_rotating { false };
    bool        m_haveLastCursor { false };
    glm::dvec2  m_lastCursor { 0.0, 0.0 };

    // Key states for continuous movement
    bool m_keyW { false }, m_keyA { false }, m_keyS { false }, m_keyD { false };
    bool m_keySpace { false }, m_keyShift { false };

    // Light
    glm::vec3 m_lightPosition;
    glm::vec3 m_lightColor;
    float     m_lightIntensity;

    // Light orb (point sprite)
    float  m_lightPointSize { 18.0f }; // pixels
    GLuint m_dummyVAO { 0 };

    // Model positioning
    float m_modelDistance { 2.0f };  // Distance between the two models

    // Matrices
    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 30.0f);
    glm::mat4 m_viewMatrix       = glm::mat4(1.0f);  // updated in update()
    glm::mat4 m_modelMatrix      { 1.0f };           // not used directly; kept for convenience

    // Timing
    double m_lastTime { 0.0 };
};

int main()
{
    Application app;
    app.update();
    return 0;
}
