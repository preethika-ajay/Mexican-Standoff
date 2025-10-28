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
#include <stb/stb_image.h>
#include <glm/gtc/random.hpp>

// ---- Multi-light globals ----
int selectedLightIndex = 0;

struct PointLight {
    glm::vec3 position { 2.0f, 3.0f, 2.0f };
    glm::vec3 color    { 1.0f, 1.0f, 1.0f };
    float     intensity{ 1.0f };
    float     pointSize{ 18.0f }; // for the on-screen orb only
};

struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    float lifetime;
    float maxLifetime;
    float size;
};

class ParticleSystem {
public:
    std::vector<Particle> particles;

    void update(float dt) {
        for (auto it = particles.begin(); it != particles.end();) {
            it->lifetime -= dt;
            if (it->lifetime <= 0.0f) {
                it = particles.erase(it);
            }
            else {
                it->position += it->velocity * dt;
                it->velocity.y -= 9.8f * dt; // gravity
                ++it;
            }
        }
    }

    void addMuzzleFlash(const glm::vec3& position) {
        // Create explosive burst of particles
        for (int i = 0; i < 20; ++i) {
            Particle p;
            p.position = position;

            // Random direction in a cone
            float angle = glm::linearRand(0.0f, glm::two_pi<float>());
            float spread = glm::linearRand(0.0f, 0.3f);
            p.velocity = glm::vec3(
                std::cos(angle) * spread,
                glm::linearRand(0.5f, 2.0f),
                std::sin(angle) * spread
            ) * 3.0f;

            p.color = glm::vec3(1.0f, glm::linearRand(0.5f, 1.0f), 0.0f); // Orange/yellow
            p.lifetime = glm::linearRand(0.1f, 0.3f);
            p.maxLifetime = p.lifetime;
            p.size = glm::linearRand(0.05f, 0.15f);

            particles.push_back(p);
        }
    }

    void addBulletTrail(const glm::vec3& position) {
        Particle p;
        p.position = position;
        p.velocity = glm::vec3(0.0f);
        p.color = glm::vec3(0.8f, 0.8f, 0.9f); // Smoke gray
        p.lifetime = 0.2f;
        p.maxLifetime = p.lifetime;
        p.size = 0.08f;
        particles.push_back(p);
    }

    void addHitEffect(const glm::vec3& position) {
        // Explosion-like effect on hit
        for (int i = 0; i < 30; ++i) {
            Particle p;
            p.position = position;

            // Spherical explosion
            float theta = glm::linearRand(0.0f, glm::two_pi<float>());
            float phi = glm::linearRand(0.0f, glm::pi<float>());
            float speed = glm::linearRand(1.0f, 4.0f);

            p.velocity = glm::vec3(
                speed * std::sin(phi) * std::cos(theta),
                speed * std::sin(phi) * std::sin(theta),
                speed * std::cos(phi)
            );

            p.color = glm::vec3(1.0f, glm::linearRand(0.0f, 0.3f), 0.0f); // Red/orange
            p.lifetime = glm::linearRand(0.3f, 0.6f);
            p.maxLifetime = p.lifetime;
            p.size = glm::linearRand(0.08f, 0.2f);

            particles.push_back(p);
        }
    }
};

struct BezierCurve {
    glm::vec3 p0, p1, p2, p3; // Control points

    // Evaluate cubic Bezier curve at parameter t (0 to 1)
    glm::vec3 evaluate(float t) const {
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float uuu = uu * u;
        float ttt = tt * t;

        glm::vec3 point = uuu * p0;           // (1-t) * P0
        point += 3.0f * uu * t * p1;          // 3(1-t)t * P1
        point += 3.0f * u * tt * p2;          // 3(1-t)t * P2
        point += ttt * p3;                     // t * P3

        return point;
    }

    // Get tangent vector at parameter t
    glm::vec3 tangent(float t) const {
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;

        glm::vec3 tangent = -3.0f * uu * p0;
        tangent += 3.0f * uu * p1 - 6.0f * u * t * p1;
        tangent += 6.0f * u * t * p2 - 3.0f * tt * p2;
        tangent += 3.0f * tt * p3;

        return glm::normalize(tangent);
    }
};

// Bullet that travels along a Bezier curve at constant speed
class Bullet {
public:
    BezierCurve curve;
    float currentDistance;
    float totalDistance;
    float speed; // units per second
    bool active;
    glm::vec3 currentPosition;
    int shooterIndex; // 0 or 1 (left or right character)

    Bullet(const glm::vec3& start, const glm::vec3& target, int shooter)
        : shooterIndex(shooter), currentDistance(0.0f), speed(10.0f), active(true)
    {
        // Create a curved bullet path (adds drama!)
        glm::vec3 mid = (start + target) * 0.5f;
        mid.y += 1.0f; // Arc upward

        // Add some random horizontal curve
        glm::vec3 perpendicular = glm::normalize(glm::cross(target - start, glm::vec3(0, 1, 0)));
        float curvature = glm::linearRand(-0.5f, 0.5f);

        curve.p0 = start;
        curve.p1 = start + (mid - start) * 0.5f + perpendicular * curvature;
        curve.p2 = mid + (target - mid) * 0.5f - perpendicular * curvature;
        curve.p3 = target;

        // Estimate arc length by sampling
        totalDistance = estimateArcLength();
        currentPosition = start;
    }

    float estimateArcLength(int samples = 50) {
        float length = 0.0f;
        glm::vec3 prev = curve.evaluate(0.0f);

        for (int i = 1; i <= samples; ++i) {
            float t = float(i) / samples;
            glm::vec3 curr = curve.evaluate(t);
            length += glm::length(curr - prev);
            prev = curr;
        }

        return length;
    }

    void update(float dt, ParticleSystem& particles) {
        if (!active) return;

        currentDistance += speed * dt;

        // Leave a trail
        if (glm::linearRand(0.0f, 1.0f) < 0.3f) {
            particles.addBulletTrail(currentPosition);
        }

        if (currentDistance >= totalDistance) {
            active = false;
            return;
        }

        // Convert distance to parametric t using binary search
        float t = distanceToParameter(currentDistance);
        currentPosition = curve.evaluate(t);
    }

    float distanceToParameter(float targetDistance, int iterations = 10) {
        float tMin = 0.0f, tMax = 1.0f;

        for (int i = 0; i < iterations; ++i) {
            float tMid = (tMin + tMax) * 0.5f;
            float dist = estimateArcLengthUpTo(tMid);

            if (dist < targetDistance) {
                tMin = tMid;
            }
            else {
                tMax = tMid;
            }
        }

        return (tMin + tMax) * 0.5f;
    }

    float estimateArcLengthUpTo(float tEnd, int samples = 20) {
        float length = 0.0f;
        glm::vec3 prev = curve.evaluate(0.0f);

        for (int i = 1; i <= samples; ++i) {
            float t = (float(i) / samples) * tEnd;
            glm::vec3 curr = curve.evaluate(t);
            length += glm::length(curr - prev);
            prev = curr;
        }

        return length;
    }

    bool checkCollision(const glm::vec3& targetPos, float radius) {
        return glm::length(currentPosition - targetPos) < radius;
    }
};

struct WesternPrimitive {
    enum Type { CUBE, CYLINDER, BLADE };
    Type type;
    glm::vec3 scale;
    glm::vec3 color;

    WesternPrimitive(Type t, glm::vec3 s, glm::vec3 c)
        : type(t), scale(s), color(c) {
    }
};

class WindmillNode {
public:
    std::string name;
    WesternPrimitive primitive;

    glm::vec3 localPosition{ 0.0f };
    glm::vec3 localRotation{ 0.0f };
    glm::vec3 localScale{ 1.0f };

    glm::vec3 rotationAxis{ 0.0f, 0.0f, 1.0f };
    float rotationSpeed{ 0.0f };
    bool animateRotation{ false };

    WindmillNode* parent{ nullptr };
    std::vector<std::unique_ptr<WindmillNode>> children;

    WindmillNode(const std::string& n, WesternPrimitive prim)
        : name(n), primitive(prim) {
    }

    WindmillNode* addChild(std::unique_ptr<WindmillNode> child) {
        child->parent = this;
        children.push_back(std::move(child));
        return children.back().get();
    }

    glm::mat4 getLocalTransform() const {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), localPosition);
        glm::mat4 Rx = glm::rotate(glm::mat4(1.0f), glm::radians(localRotation.x), glm::vec3(1, 0, 0));
        glm::mat4 Ry = glm::rotate(glm::mat4(1.0f), glm::radians(localRotation.y), glm::vec3(0, 1, 0));
        glm::mat4 Rz = glm::rotate(glm::mat4(1.0f), glm::radians(localRotation.z), glm::vec3(0, 0, 1));
        glm::mat4 S = glm::scale(glm::mat4(1.0f), localScale);
        return T * Rz * Ry * Rx * S;
    }

    glm::mat4 getWorldTransform() const {
        if (parent) {
            return parent->getWorldTransform() * getLocalTransform();
        }
        return getLocalTransform();
    }

    void updateAnimation(float dt) {
        if (animateRotation && rotationSpeed != 0.0f) {
            if (rotationAxis.x > 0.5f) {
                localRotation.x += rotationSpeed * dt;
            }
            else if (rotationAxis.y > 0.5f) {
                localRotation.y += rotationSpeed * dt;
            }
            else if (rotationAxis.z > 0.5f) {
                localRotation.z += rotationSpeed * dt;
            }
        }

        for (auto& child : children) {
            child->updateAnimation(dt);
        }
    }
};

class WesternWindmill {
public:
    std::unique_ptr<WindmillNode> root;
    bool animationEnabled{ true };
    float windSpeed{ 20.0f };

    WesternWindmill() {
        buildWindmill();
    }

void buildWindmill() {
    // ── TOWER BASE (brown)
    root = std::make_unique<WindmillNode>(
        "TowerBase",
        WesternPrimitive(WesternPrimitive::CUBE,
            /*scale*/ glm::vec3(1.25f, 0.60f, 1.25f),
            /*color*/ glm::vec3(0.45f, 0.30f, 0.18f))   // dark brown
    );
    root->localPosition = glm::vec3(0.0f, -0.30f, 0.0f);

    // ── TOWER MIDDLE (lighter brown)
    auto towerMid = std::make_unique<WindmillNode>(
        "TowerMiddle",
        WesternPrimitive(WesternPrimitive::CYLINDER,
            /*scale*/ glm::vec3(0.95f, 2.60f, 0.95f),
            /*color*/ glm::vec3(0.62f, 0.43f, 0.24f))   // mid brown
    );
    towerMid->localPosition = glm::vec3(0.0f, 0.60f - 0.02f, 0.0f);

    // ── TOWER TOP (extended downward; reddish tint)
    auto towerTop = std::make_unique<WindmillNode>(
        "TowerTop",
        WesternPrimitive(WesternPrimitive::CYLINDER,
            /*scale*/ glm::vec3(0.90f, 2.20f, 0.90f),   // taller so it reaches downward
            /*color*/ glm::vec3(0.55f, 0.25f, 0.25f))   // warm red-brown
    );
    // place so the *bottom* sinks into towerMid a bit (no seam)
    towerTop->localPosition = glm::vec3(0.0f, 2.60f - 0.20f, 0.0f);

    // ── PLATFORM (extended toward hub; cool steel)
    auto platform = std::make_unique<WindmillNode>(
        "Platform",
        WesternPrimitive(WesternPrimitive::CUBE,
            /*scale*/ glm::vec3(1.60f, 0.35f, 2.00f),   // deeper along +Z toward the hub
            /*color*/ glm::vec3(0.35f, 0.45f, 0.55f))   // steel blue/gray
    );
    // sit near top of towerTop and nudge forward so its front approaches the hub
    platform->localPosition = glm::vec3(0.0f, 0.95f - 0.02f, 0.15f);

    // ── HUB (charcoal)
    auto hub = std::make_unique<WindmillNode>(
        "Hub",
        WesternPrimitive(WesternPrimitive::CYLINDER,
            /*scale*/ glm::vec3(0.50f, 0.50f, 0.50f),
            /*color*/ glm::vec3(0.18f, 0.18f, 0.20f))   // charcoal
    );
    // keep it clearly in front; platform depth now reaches closer to it
    hub->localPosition   = glm::vec3(0.0f, 0.38f, 1.05f);
    hub->animateRotation = true;
    hub->rotationAxis    = glm::vec3(0.0f, 0.0f, 1.0f);
    hub->rotationSpeed   = windSpeed;

    // ── BLADES (light wood)
    const glm::vec3 bladeScale(0.22f, 1.95f, 0.08f);
    const glm::vec3 bladeColor(0.80f, 0.72f, 0.58f);    // light wood

    auto blade1 = std::make_unique<WindmillNode>(
        "Blade1", WesternPrimitive(WesternPrimitive::BLADE, bladeScale, bladeColor));
    blade1->localPosition = glm::vec3(0.0f, 0.98f, 0.0f);

    auto blade2 = std::make_unique<WindmillNode>(
        "Blade2", WesternPrimitive(WesternPrimitive::BLADE, bladeScale, bladeColor));
    blade2->localPosition = glm::vec3(0.98f, 0.0f, 0.0f);
    blade2->localRotation = glm::vec3(0.0f, 0.0f, 90.0f);

    auto blade3 = std::make_unique<WindmillNode>(
        "Blade3", WesternPrimitive(WesternPrimitive::BLADE, bladeScale, bladeColor));
    blade3->localPosition = glm::vec3(0.0f, -0.98f, 0.0f);
    blade3->localRotation = glm::vec3(0.0f, 0.0f, 180.0f);

    auto blade4 = std::make_unique<WindmillNode>(
        "Blade4", WesternPrimitive(WesternPrimitive::BLADE, bladeScale, bladeColor));
    blade4->localPosition = glm::vec3(-0.98f, 0.0f, 0.0f);
    blade4->localRotation = glm::vec3(0.0f, 0.0f, 270.0f);

    // ── SUPPORT STRUTS (bronze)
    auto strut1 = std::make_unique<WindmillNode>(
        "Strut1",
        WesternPrimitive(WesternPrimitive::CYLINDER,
            /*scale*/ glm::vec3(0.16f, 1.55f, 0.16f),
            /*color*/ glm::vec3(0.60f, 0.42f, 0.20f))   // bronze/wood
    );
    strut1->localPosition = glm::vec3(0.52f, -0.35f, 0.55f);
    strut1->localRotation = glm::vec3(45.0f, 0.0f, 30.0f);

    auto strut2 = std::make_unique<WindmillNode>(
        "Strut2",
        WesternPrimitive(WesternPrimitive::CYLINDER,
            /*scale*/ glm::vec3(0.16f, 1.55f, 0.16f),
            /*color*/ glm::vec3(0.60f, 0.42f, 0.20f))
    );
    strut2->localPosition = glm::vec3(-0.52f, -0.35f, 0.55f);
    strut2->localRotation = glm::vec3(45.0f, 0.0f, -30.0f);

    // ── hierarchy
    hub->addChild(std::move(blade1));
    hub->addChild(std::move(blade2));
    hub->addChild(std::move(blade3));
    hub->addChild(std::move(blade4));

    platform->addChild(std::move(hub));
    platform->addChild(std::move(strut1));
    platform->addChild(std::move(strut2));

    towerTop->addChild(std::move(platform));
    towerMid->addChild(std::move(towerTop));
    root->addChild(std::move(towerMid));
}


    void update(float dt) {
        if (animationEnabled && root) {
            updateWindSpeed(windSpeed);
            root->updateAnimation(dt);
        }
    }

    void updateWindSpeed(float speed) {
        if (root && !root->children.empty()) {
            auto* towerMid = root->children[0].get();
            if (!towerMid->children.empty()) {
                auto* towerTop = towerMid->children[0].get();
                if (!towerTop->children.empty()) {
                    auto* platform = towerTop->children[0].get();
                    if (!platform->children.empty()) {
                        auto* hub = platform->children[0].get();
                        hub->rotationSpeed = speed;
                    }
                }
            }
        }
    }

    void collectRenderData(std::vector<std::pair<WindmillNode*, glm::mat4>>& outData) {
        if (root) {
            collectNodeRenderData(root.get(), outData);
        }
    }

private:
    void collectNodeRenderData(WindmillNode* node, std::vector<std::pair<WindmillNode*, glm::mat4>>& outData) {
        outData.push_back({ node, node->getWorldTransform() });
        for (auto& child : node->children) {
            collectNodeRenderData(child.get(), outData);
        }
    }
};

class BezierPath {
public:
    std::vector<BezierCurve> curves;

    BezierPath() {
        // Create a smooth camera path with 3+ cubic Bzier curves
        // These create a swooping camera motion around the scene

        // Curve 1: Start -> upper right
        curves.push_back({
            glm::vec3(5.0f, 3.0f, 5.0f),   // p0
            glm::vec3(5.0f, 5.0f, 3.0f),   // p1
            glm::vec3(3.0f, 6.0f, 0.0f),   // p2
            glm::vec3(0.0f, 5.0f, -3.0f)   // p3
            });

        // Curve 2: Right -> back
        curves.push_back({
            glm::vec3(0.0f, 5.0f, -3.0f),   // p0 (same as previous p3)
            glm::vec3(-2.0f, 4.0f, -5.0f),  // p1
            glm::vec3(-4.0f, 3.0f, -5.0f),  // p2
            glm::vec3(-5.0f, 2.0f, -3.0f)   // p3
            });

        // Curve 3: Back -> left
        curves.push_back({
            glm::vec3(-5.0f, 2.0f, -3.0f),  // p0
            glm::vec3(-6.0f, 2.0f, 0.0f),   // p1
            glm::vec3(-5.0f, 3.0f, 3.0f),   // p2
            glm::vec3(-3.0f, 4.0f, 5.0f)    // p3
            });

        // Curve 4: Left -> front (completing the loop)
        curves.push_back({
            glm::vec3(-3.0f, 4.0f, 5.0f),   // p0
            glm::vec3(0.0f, 5.0f, 6.0f),    // p1
            glm::vec3(3.0f, 4.0f, 6.0f),    // p2
            glm::vec3(5.0f, 3.0f, 5.0f)     // p3 (loops back to start)
            });
    }

    // Get total number of curves
    int numCurves() const {
        return static_cast<int>(curves.size());
    }

    // Evaluate path at global parameter t (0 to numCurves)
    glm::vec3 evaluate(float t) const {
        if (curves.empty()) return glm::vec3(0.0f);

        // Wrap t to loop the path
        t = std::fmod(t, static_cast<float>(curves.size()));
        if (t < 0.0f) t += curves.size();

        int curveIndex = static_cast<int>(std::floor(t));
        float localT = t - curveIndex;

        curveIndex = curveIndex % curves.size();
        return curves[curveIndex].evaluate(localT);
    }

    // Get tangent at global parameter t
    glm::vec3 tangent(float t) const {
        if (curves.empty()) return glm::vec3(0.0f, 0.0f, 1.0f);

        t = std::fmod(t, static_cast<float>(curves.size()));
        if (t < 0.0f) t += curves.size();

        int curveIndex = static_cast<int>(std::floor(t));
        float localT = t - curveIndex;

        curveIndex = curveIndex % curves.size();
        return curves[curveIndex].tangent(localT);
    }

    // Generate line segments for rendering the curve
    std::vector<glm::vec3> generateLineSegments(int segmentsPerCurve = 20) const {
        std::vector<glm::vec3> points;

        for (const auto& curve : curves) {
            for (int i = 0; i <= segmentsPerCurve; ++i) {
                float t = static_cast<float>(i) / segmentsPerCurve;
                points.push_back(curve.evaluate(t));
            }
        }

        return points;
    }
};

class Application {
public:
    Application()
        : m_window("Final Project", glm::ivec2(1024, 1024), OpenGLVersion::GL41)
        , m_texture(RESOURCE_ROOT "resources/grass_albedo.png")
        , m_normalMap(RESOURCE_ROOT "resources/brick_normal.png") 
        , m_diffuseTexture(RESOURCE_ROOT "resources/brick_normal.png")
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
            int w = std::max(size.x, 1);
            int h = std::max(size.y, 1);
            glViewport(0, 0, w, h);
            float aspect = float(w) / float(h);
            m_projectionMatrix = glm::perspective(glm::radians(80.0f), aspect, 0.1f, 30.0f);
            });

        m_window.registerScrollCallback(std::bind(&Application::onScroll, this, std::placeholders::_1));

        // Load models
        m_meshesA = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/gunslinger_arms_down_both.obj");
        m_meshesB = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/gunslinger_arms_down_both.obj");
        ground    = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/ground_plane.obj");

        std::vector<std::string> faces = {
        RESOURCE_ROOT "resources/right.jpg",   // positive x
        RESOURCE_ROOT "resources/left.jpg",    // negative x
        RESOURCE_ROOT "resources/top.jpg",     // positive y
        RESOURCE_ROOT "resources/bottom.jpg",  // negative y
        RESOURCE_ROOT "resources/front.jpg",   // positive z
        RESOURCE_ROOT "resources/back.jpg"     // negative z
        };
        m_cubemapTexture = loadCubemap(faces);

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

            ShaderBuilder pbrBuilder;
            pbrBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/pbr_vert.glsl");
            pbrBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/pbr_frag.glsl");
            m_pbrShader = pbrBuilder.build();

            ShaderBuilder pathBuilder;
            pathBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/path_vert.glsl");
            pathBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/path_frag.glsl");
            m_pathShader = pathBuilder.build();

            // Initialize path rendering
            initPathRendering();

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
        initWindmillPrimitives();
        initParticleRendering();
    }

    void shootBullet(int shooterIndex) {
        glm::vec3 leftPos(-m_modelDistance * 0.5f, 1.0f, 0.0f);   // ? Changed from 1.5f to 1.0f
        glm::vec3 rightPos(m_modelDistance * 0.5f, 1.0f, 0.0f);   // ? Changed from 1.5f to 1.0f

        glm::vec3 start, target;
        if (shooterIndex == 0) {
            start = leftPos + glm::vec3(0.5f, 0.0f, 0.0f); // Gun position
            target = rightPos;
        }
        else {
            start = rightPos + glm::vec3(-0.5f, 0.0f, 0.0f);
            target = leftPos;
        }

        m_bullets.emplace_back(start, target, shooterIndex);
        m_particleSystem.addMuzzleFlash(start);
    }

    void updateBulletsAndCollisions(float dt) {
        m_particleSystem.update(dt);

        // Auto-shoot mode
        if (m_autoShoot) {
            m_shootTimer -= dt;
            if (m_shootTimer <= 0.0f) {
                int shooter = glm::linearRand(0, 1);
                shootBullet(shooter);
                m_shootTimer = glm::linearRand(1.0f, 3.0f);
            }
        }

        glm::vec3 leftPos(-m_modelDistance * 0.5f, 1.0f, 0.0f);   
        glm::vec3 rightPos(m_modelDistance * 0.5f, 1.0f, 0.0f);  

        for (auto& bullet : m_bullets) {
            bullet.update(dt, m_particleSystem);

            if (!bullet.active) continue;

            // Check collision with target character
            glm::vec3 targetPos = (bullet.shooterIndex == 0) ? rightPos : leftPos;
            int targetIndex = (bullet.shooterIndex == 0) ? 1 : 0;

            if (!m_characterHit[targetIndex] && bullet.checkCollision(targetPos, 0.5f)) {
                bullet.active = false;
                m_characterHit[targetIndex] = true;
                m_particleSystem.addHitEffect(bullet.currentPosition);
                std::cout << "Character " << targetIndex << " hit!\n";
            }
        }

        // Remove inactive bullets
        m_bullets.erase(
            std::remove_if(m_bullets.begin(), m_bullets.end(),
                [](const Bullet& b) { return !b.active; }),
            m_bullets.end()
        );
    }

    void renderParticles() {
        if (m_particleSystem.particles.empty()) return;

        m_lightPointShader.bind();
        glBindVertexArray(m_particleVAO);

        glm::mat4 viewProj = m_projectionMatrix * m_viewMatrix;
        glUniformMatrix4fv(m_lightPointShader.getUniformLocation("viewProj"),
            1, GL_FALSE, glm::value_ptr(viewProj));

        for (const auto& p : m_particleSystem.particles) {
            float alpha = p.lifetime / p.maxLifetime;
            glm::vec3 color = p.color * alpha;

            glUniform3fv(m_lightPointShader.getUniformLocation("lightPos"),
                1, glm::value_ptr(p.position));
            glUniform1f(m_lightPointShader.getUniformLocation("pointSize"),
                p.size * 100.0f);
            glUniform3fv(m_lightPointShader.getUniformLocation("lightColor"),
                1, glm::value_ptr(color));

            glDrawArrays(GL_POINTS, 0, 1);
        }

        glBindVertexArray(0);
    }

    void renderBullets() {
        m_lightPointShader.bind();
        glBindVertexArray(m_dummyVAO);

        glm::mat4 viewProj = m_projectionMatrix * m_viewMatrix;
        glUniformMatrix4fv(m_lightPointShader.getUniformLocation("viewProj"),
            1, GL_FALSE, glm::value_ptr(viewProj));

        for (const auto& bullet : m_bullets) {
            if (!bullet.active) continue;

            glUniform3fv(m_lightPointShader.getUniformLocation("lightPos"),
                1, glm::value_ptr(bullet.currentPosition));
            glUniform1f(m_lightPointShader.getUniformLocation("pointSize"), 20.0f);
            glUniform3fv(m_lightPointShader.getUniformLocation("lightColor"),
                1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.0f)));

            glDrawArrays(GL_POINTS, 0, 1);
        }

        glBindVertexArray(0);
    }

    void initParticleRendering() {
        glGenVertexArrays(1, &m_particleVAO);
    }

    void update()
    {
        int dummyInteger = 0;
        while (!m_window.shouldClose()) {
            // Input/update
            m_window.updateInput();

            const double now = glfwGetTime();
            float dt = static_cast<float>(now - m_lastTime);
            if (!m_walkInitDone) {
                m_modelDistance = m_startSep;
                m_walkInitDone  = true;
            }

            // Walk away from each other (backs to each other)
            if (m_walkAway) {
                // distance between the two doubles because both walk outwards
                m_modelDistance = std::min(m_modelDistance + 2.0f * m_walkSpeed * dt, m_maxSep);
            }

            // If we just reached max separation, switch models and enable shooting once
            if (!m_startedShootPhase && m_modelDistance >= m_maxSep - 1e-4f) {
                enterShootPhase();
            }

            m_lastTime = now;
            dt = glm::clamp(dt, 0.0f, 0.05f);

            handleContinuousKeyboard(dt);
            updatePathCamera(dt);
            m_windmill.update(dt);
            updateBulletsAndCollisions(dt);

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
            ImGui::SliderFloat("Shininess", &m_shininess, 1.0f, 256.0f);
            ImGui::Checkbox("Enable Normal Map", &m_enableNormalMap);
            ImGui::Checkbox("Enable Diffuse Texture", &m_enableDiffuseTexture);

            if (m_enablePBR) {
                ImGui::SliderFloat("Metallic", &m_metallic, 0.0f, 1.0f);
                ImGui::SliderFloat("Roughness", &m_roughness, 0.05f, 1.0f);
                ImGui::ColorEdit3("Albedo", glm::value_ptr(m_albedo));
            }

            ImGui::Checkbox("Enable Environment Map", &m_enableEnvironmentMap);  // ? Add this
            if (m_enableEnvironmentMap) {
                ImGui::SliderFloat("Reflectivity", &m_reflectivity, 0.0f, 1.0f);  // ? Add this
            }

            ImGui::Separator();
            ImGui::Text("Bezier Camera Path");
            ImGui::Checkbox("Follow Path", &m_followPath);
            ImGui::Checkbox("Show Path Curve", &m_showPathCurve);
            if (m_followPath) {
                ImGui::SliderFloat("Path Speed", &m_pathSpeed, 0.1f, 2.0f);
                ImGui::Text("Path Time: %.2f", m_pathTime);
                if (ImGui::Button("Reset Path Position")) {
                    m_pathTime = 0.0f;
                }
            }

            ImGui::Separator();
            ImGui::Text("Western Windmill");
            ImGui::Checkbox("Enable Windmill Animation", &m_windmill.animationEnabled);
            ImGui::SliderFloat("Wind Speed", &m_windmill.windSpeed, 0.0f, 100.0f);
            for (size_t i = 0; i < m_windmillPositions.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::DragFloat3("Windmill Position",
                                  glm::value_ptr(m_windmillPositions[i]),
                                  0.1f, -20.0f, 20.0f);
                ImGui::PopID();
            }

            ImGui::Checkbox("Walk Away (auto)", &m_walkAway);
            ImGui::SliderFloat("Walk Speed (u/s per char)", &m_walkSpeed, 0.0f, 3.0f);
            ImGui::SliderFloat("Max Separation", &m_maxSep, 0.5f, 20.0f);
            ImGui::Text("Current Separation: %.2f", m_modelDistance);

            ImGui::Text("Phase: %s", m_startedShootPhase ? "Shoot" : "Walk");


            ImGui::Separator();
            ImGui::Text("Mexican Standoff Controls");
            ImGui::Checkbox("Auto-Shoot Mode", &m_autoShoot);
            if (ImGui::Button("Left Character Shoots")) {
                shootBullet(0);
            }
            ImGui::SameLine();
            if (ImGui::Button("Right Character Shoots")) {
                shootBullet(1);
            }
            if (ImGui::Button("Reset Standoff")) {
                m_bullets.clear();
                m_particleSystem.particles.clear();
                m_characterHit[0] = false;
                m_characterHit[1] = false;
            }

            ImGui::Text("Left Character: %s", m_characterHit[0] ? "HIT" : "OK");
            ImGui::Text("Right Character: %s", m_characterHit[1] ? "HIT" : "OK");
            ImGui::Text("Active Bullets: %zu", m_bullets.size());
            ImGui::Text("Active Particles: %zu", m_particleSystem.particles.size());

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

            ImGui::Separator();
            ImGui::Text("Viewpoints");
            const char* viewItems[] = { "Default", "Top", "Third-Person" };
            int currentView = static_cast<int>(m_viewMode);
            if (ImGui::Combo("Active View", &currentView, viewItems, IM_ARRAYSIZE(viewItems))) {
                m_viewMode = static_cast<ViewMode>(currentView);
            }

            ImGui::Separator();
            ImGui::Checkbox("Enable Trackball Camera", &m_trackballEnabled);
            ImGui::SliderFloat("Trackball Distance", &m_distanceFromTarget, 1.0f, 20.0f);

            ImGui::Separator();
            ImGui::Text("Advanced Shading");
            ImGui::Checkbox("Enable PBR Shader", &m_enablePBR);

            ImGui::End();

            updateCameraViewMode();

            // Resize shadow maps if needed
            resizeShadowResourcesIfNeeded();

            // PASS 1: render shadow maps (full body casts)
            renderShadowMaps();

            // PASS 2: render scene
            glViewport(0, 0, m_window.getWindowSize().x, m_window.getWindowSize().y);
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            renderLightPoints();
            renderParticles();
            renderBullets();
            renderPathCurve();

            // Ground (receives shadows)
            renderModel(ground, glm::mat4(1.0f), /*isGround*/true);

            // Characters (occluders only, not receivers)
            // Back-to-back orientation: left +90°, right -90° around Y
            glm::mat4 rotLeft  = glm::rotate(glm::mat4(1.0f), glm::radians( 90.0f), glm::vec3(0,1,0));
            glm::mat4 rotRight = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,1,0));

            glm::mat4 modelMatrixA =
                glm::translate(glm::mat4(1.0f), glm::vec3(-m_modelDistance * 0.5f, 0.0f, 0.0f)) * rotLeft;
            glm::mat4 modelMatrixB =
                glm::translate(glm::mat4(1.0f), glm::vec3( m_modelDistance * 0.5f, 0.0f, 0.0f)) * rotRight;

            if (m_applyShootRot) {
                // Left model:  -90° about Z
                glm::mat4 zLeft  = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,1,0));
                // Right model: +90° about Z
                glm::mat4 zRight = glm::rotate(glm::mat4(1.0f), glm::radians( 90.0f), glm::vec3(0,1, 0));

                modelMatrixA = modelMatrixA * zLeft;
                modelMatrixB = modelMatrixB * zRight;
            }

            renderModel(m_meshesA, modelMatrixA, /*isGround*/false);
            renderModel(m_meshesB, modelMatrixB, /*isGround*/false);

            renderWindmills();

            m_window.swapBuffers();
        }
    }

    void renderWindmills() {
    // gather nodes once
    std::vector<std::pair<WindmillNode*, glm::mat4>> renderData;
    m_windmill.collectRenderData(renderData);

    Shader& shader = m_defaultShader;
    shader.bind();

    // lights (same as before)
    int n = std::min((int)m_lights.size(), MAX_LIGHTS);
    std::vector<glm::vec3> lp(n), lc(n);
    std::vector<float>     li(n);
    for (int i = 0; i < n; ++i) { lp[i]=m_lights[i].position; lc[i]=m_lights[i].color; li[i]=m_lights[i].intensity; }

    glUniform1i(shader.getUniformLocation("numLights"), n);
    if (n > 0) {
        glUniform3fv(shader.getUniformLocation("lightPosition"),  n, glm::value_ptr(lp[0]));
        glUniform3fv(shader.getUniformLocation("lightColor"),     n, glm::value_ptr(lc[0]));
        glUniform1fv(shader.getUniformLocation("lightIntensity"), n, li.data());
    }
    glUniform3fv(shader.getUniformLocation("viewPosition"), 1, glm::value_ptr(m_cameraPosition));
    glUniform1f(shader.getUniformLocation("kd"), 0.7f);
    glUniform1f(shader.getUniformLocation("ks"), 0.1f);
    glUniform1f(shader.getUniformLocation("shininess"), 8.0f);
    glUniform1i(shader.getUniformLocation("hasTexCoords"), GL_FALSE);
    glUniform1i(shader.getUniformLocation("useMaterial"),  GL_TRUE);
    glUniform1i(shader.getUniformLocation("isGround"),     0);

    // explicitly disable shadow sampling for windmills
    glUniform1i(shader.getUniformLocation("numShadowMaps"), 0);

    glBindVertexArray(m_cubeVAO);

    for (const glm::vec3& pos : m_windmillPositions) {
        glm::mat4 world = glm::translate(glm::mat4(1.0f), pos);

        for (auto& [node, local] : renderData) {
            glm::mat4 model = world * local * glm::scale(glm::mat4(1.0f), node->primitive.scale);
            glm::mat4 mvp   = m_projectionMatrix * m_viewMatrix * model;
            glm::mat3 nrm   = glm::inverseTranspose(glm::mat3(model));

            glUniformMatrix4fv(shader.getUniformLocation("mvpMatrix"),         1, GL_FALSE, glm::value_ptr(mvp));
            glUniformMatrix4fv(shader.getUniformLocation("modelMatrix"),       1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(shader.getUniformLocation("normalModelMatrix"), 1, GL_FALSE, glm::value_ptr(nrm));
            glUniform3fv(shader.getUniformLocation("materialColor"),           1, glm::value_ptr(node->primitive.color));

            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }
    }

    glBindVertexArray(0);
}

    void initPathRendering() {
        m_pathLineSegments = m_cameraPath.generateLineSegments(30); // 30 segments per curve

        glGenVertexArrays(1, &m_pathVAO);
        glGenBuffers(1, &m_pathVBO);

        glBindVertexArray(m_pathVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_pathVBO);
        glBufferData(GL_ARRAY_BUFFER,
            m_pathLineSegments.size() * sizeof(glm::vec3),
            m_pathLineSegments.data(),
            GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

        glBindVertexArray(0);
    }

    void updatePathCamera(float dt) {
        if (!m_followPath) return;

        m_pathTime += dt * m_pathSpeed;

        // Update camera position from path
        m_cameraPosition = m_cameraPath.evaluate(m_pathTime);

        // Look at scene center
        glm::vec3 sceneCenter(0.0f, 1.0f, 0.0f);
        m_cameraFront = glm::normalize(sceneCenter - m_cameraPosition);

        // Update yaw/pitch to match
        m_yaw = glm::degrees(std::atan2(m_cameraFront.z, m_cameraFront.x));
        m_pitch = glm::degrees(std::asin(glm::clamp(m_cameraFront.y, -1.0f, 1.0f)));
    }

    void renderPathCurve() {
        if (!m_showPathCurve) return;

        m_pathShader.bind();

        glm::mat4 vp = m_projectionMatrix * m_viewMatrix;
        glUniformMatrix4fv(m_pathShader.getUniformLocation("viewProj"),
            1, GL_FALSE, glm::value_ptr(vp));

        glm::vec3 pathColor(1.0f, 1.0f, 0.0f); // Yellow
        glUniform3fv(m_pathShader.getUniformLocation("pathColor"),
            1, glm::value_ptr(pathColor));

        glBindVertexArray(m_pathVAO);
        glLineWidth(3.0f);
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(m_pathLineSegments.size()));
        glLineWidth(1.0f);
        glBindVertexArray(0);
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
    Shader m_pbrShader;
    bool   m_enablePBR{ false };

    float m_metallic{ 0.2f };
    float m_roughness{ 0.4f };
    glm::vec3 m_albedo{ 0.8f, 0.6f, 0.4f };

    GLuint m_cubemapTexture{ 0 };
    bool m_enableEnvironmentMap{ false };
    float m_reflectivity{ 0.5f };

    BezierPath m_cameraPath;
    bool m_followPath{ false };
    bool m_showPathCurve{ true };
    float m_pathTime{ 0.0f };
    float m_pathSpeed{ 0.5f };
    GLuint m_pathVAO{ 0 };
    GLuint m_pathVBO{ 0 };
    std::vector<glm::vec3> m_pathLineSegments;
    Shader m_pathShader;

    // --- Walk-away behaviour ---
    bool  m_walkAway     { true };   // start with auto-walk enabled
    float m_startSep     { 0.60f };  // tiny initial gap
    float m_walkSpeed    { 0.25f };   // units/s per character
    float m_maxSep       { 6.0f };   // clamp maximum distance
    bool  m_walkInitDone { false };  // one-time init flag

    // --- Shoot phase trigger ---
    bool  m_startedShootPhase { false };

    // Optional: alternate meshes to use once separated
    std::vector<GPUMesh> m_meshesA_aim, m_meshesB_aim;

    // File paths for the “aiming” (or alternate) meshes.
    // Replace with whatever filenames you actually have.
    const char* m_leftAimMeshPath  = RESOURCE_ROOT "resources/gunslinger_cylindrical_v3_caps.obj";
    const char* m_rightAimMeshPath = RESOURCE_ROOT "resources/gunslinger_cylindrical_v3_caps_mirrorX.obj";


    WesternWindmill m_windmill;
    std::vector<glm::vec3> m_windmillPositions{
        { -12.0f, 0.0f, -20.0f },
        {  -6.0f, 0.0f, -22.0f },
        {   0.0f, 0.0f, -24.0f },
        {   6.0f, 0.0f, -22.0f },
        {  12.0f, 0.0f, -20.0f }
    };
    GLuint m_cubeVAO{ 0 }, m_cubeVBO{ 0 }, m_cubeIBO{ 0 };

     ParticleSystem m_particleSystem;
     std::vector<Bullet> m_bullets;
     GLuint m_particleVAO{0};
     bool m_autoShoot{false};
     float m_shootTimer{0.0f};
     bool m_characterHit[2] = {false, false};
    bool m_applyShootRot{ false };

    GLuint loadCubemap(const std::vector<std::string>& faces)
    {
        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

        int width, height, nrChannels;
        for (unsigned int i = 0; i < faces.size(); i++)
        {
            unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 3); // Force 3 channels (RGB)
            if (data)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                    0, GL_RGB, width, height, 0,
                    GL_RGB, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
                std::cout << "Loaded cubemap face " << i << ": " << faces[i] << std::endl;
            }
            else
            {
                std::cerr << "Failed to load cubemap face: " << faces[i] << std::endl;
                stbi_image_free(data);
            }
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        return textureID;
    }

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
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // depth=1 => fully lit outside
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

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

    void initWindmillPrimitives() {
        float cubeVertices[] = {
            -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,
            -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f
        };

        unsigned int cubeIndices[] = {
            0,1,2, 2,3,0, 1,5,6, 6,2,1, 5,4,7, 7,6,5,
            4,0,3, 3,7,4, 3,2,6, 6,7,3, 4,5,1, 1,0,4
        };

        glGenVertexArrays(1, &m_cubeVAO);
        glGenBuffers(1, &m_cubeVBO);
        glGenBuffers(1, &m_cubeIBO);

        glBindVertexArray(m_cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeIBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

        glBindVertexArray(0);
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
                glUniformMatrix4fv(m_shadowShader.getUniformLocation("lightMVP"),
                                   1, GL_FALSE, glm::value_ptr(mvp));
                for (auto& m : meshes) m.draw(m_shadowShader);
            };

            glm::mat4 rotLeft  = glm::rotate(glm::mat4(1.0f), glm::radians( 90.0f), glm::vec3(0,1,0));
            glm::mat4 rotRight = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,1,0));

            glm::mat4 modelMatrixA =
                glm::translate(glm::mat4(1.0f), glm::vec3(-m_modelDistance * 0.5f, 0.0f, 0.0f)) * rotLeft;
            glm::mat4 modelMatrixB =
                glm::translate(glm::mat4(1.0f), glm::vec3( m_modelDistance * 0.5f, 0.0f, 0.0f)) * rotRight;

            // Characters into shadow map
            drawDepth(m_meshesA, modelMatrixA);
            drawDepth(m_meshesB, modelMatrixB);


            //drawWindmillDepth(m_lightViewProj[i]);
        }


        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    void enterShootPhase()
    {
        if (m_startedShootPhase) return;
        m_startedShootPhase = true;

        // Stop walking exactly at max separation so they don't drift
        m_walkAway    = false;
        m_modelDistance = m_maxSep;

        // Try to load alternate “aiming” meshes. If unavailable, silently keep current ones.
        try {
            m_meshesA_aim = GPUMesh::loadMeshGPU(m_leftAimMeshPath);
        } catch (...) {
            m_meshesA_aim.clear();
        }
        try {
            m_meshesB_aim = GPUMesh::loadMeshGPU(m_rightAimMeshPath);
        } catch (...) {
            m_meshesB_aim.clear();
        }

        // If both alternates loaded, swap them in
        if (!m_meshesA_aim.empty()) m_meshesA = std::move(m_meshesA_aim);
        if (!m_meshesB_aim.empty()) m_meshesB = std::move(m_meshesB_aim);

        // Flip on auto-shoot
        m_autoShoot  = true;

        // Optional: quick first shot so it feels immediate
        m_shootTimer = 0.2f; // first random shot soon

        // >>> enable the Z-rotations after we’ve switched
        m_applyShootRot = true;
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
        const glm::mat4 mvpMatrix = m_projectionMatrix * m_viewMatrix * modelMatrix;
        const glm::mat3 normalModelMatrix = glm::inverseTranspose(glm::mat3(modelMatrix));

        // Lights
        int n = (int)std::min<size_t>(m_lights.size(), MAX_LIGHTS);
        glm::vec3 lp[MAX_LIGHTS], lc[MAX_LIGHTS];
        float li[MAX_LIGHTS];
        for (int i = 0; i < n; ++i) { lp[i] = m_lights[i].position; lc[i] = m_lights[i].color; li[i] = m_lights[i].intensity; }

        // Shadows
        int sN = std::min(n, MAX_SHADOW_LIGHTS);
        glm::mat4 lvp[MAX_SHADOW_LIGHTS];
        for (int i = 0; i < sN; ++i) lvp[i] = m_lightViewProj[i];

        // Bind shadow maps to texture units 1..N
        for (int i = 0; i < sN; ++i) {
            glActiveTexture(GL_TEXTURE1 + i);
            glBindTexture(GL_TEXTURE_2D, m_shadowDepth[i]);
        }

        glActiveTexture(GL_TEXTURE0);

        for (GPUMesh& mesh : meshes) {
            Shader& shader = m_enablePBR ? m_pbrShader : m_defaultShader;
            shader.bind();

            // ? ADD ALL THESE UNIFORMS (you had these before but removed them)
            glUniformMatrix4fv(shader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(mvpMatrix));
            glUniformMatrix4fv(shader.getUniformLocation("modelMatrix"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
            glUniformMatrix3fv(shader.getUniformLocation("normalModelMatrix"), 1, GL_FALSE, glm::value_ptr(normalModelMatrix));

            glUniform1i(shader.getUniformLocation("numLights"), n);
            if (n > 0) {
                glUniform3fv(shader.getUniformLocation("lightPosition"), n, glm::value_ptr(lp[0]));
                glUniform3fv(shader.getUniformLocation("lightColor"), n, glm::value_ptr(lc[0]));
                glUniform1fv(shader.getUniformLocation("lightIntensity"), n, li);
            }
            glUniform3fv(shader.getUniformLocation("viewPosition"), 1, glm::value_ptr(m_cameraPosition));

            glUniform1f(shader.getUniformLocation("kd"), m_kd);
            glUniform1f(shader.getUniformLocation("ks"), m_ks);
            glUniform1f(shader.getUniformLocation("shininess"), m_shininess);

            glUniform1i(shader.getUniformLocation("numShadowMaps"), sN);
            if (sN > 0) {
                for (int i = 0; i < sN; ++i) {
                    std::string name = "shadowMaps[" + std::to_string(i) + "]";
                    glUniform1i(shader.getUniformLocation(name.c_str()), 1 + i);
                }
                glUniformMatrix4fv(shader.getUniformLocation("lightViewProj"), sN, GL_FALSE, glm::value_ptr(lvp[0]));
                glUniform2f(shader.getUniformLocation("shadowTexelSize"),
                    m_pcfTexelScale / float(m_shadowMapSize),
                    m_pcfTexelScale / float(m_shadowMapSize));
                glUniform1f(shader.getUniformLocation("shadowBias"), m_shadowBias);
                glUniform1f(shader.getUniformLocation("shadowMinNdotL"), m_shadowMinNdotL);
            }
            glUniform1i(shader.getUniformLocation("isGround"), isGround ? 1 : 0);

            glm::vec3 matColor = glm::vec3(0.75f, 0.75f, 0.78f); // soft neutral gray
            glUniform3fv(shader.getUniformLocation("materialColor"), 1, glm::value_ptr(matColor));

            if (m_enableEnvironmentMap) {
                int cubemapUnit = MAX_SHADOW_LIGHTS + 2;  // After normal map
                glActiveTexture(GL_TEXTURE0 + cubemapUnit);
                glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemapTexture);
                glUniform1i(shader.getUniformLocation("environmentMap"), cubemapUnit);
                glUniform1i(shader.getUniformLocation("enableEnvironmentMap"), GL_TRUE);
                glUniform1f(shader.getUniformLocation("reflectivity"), m_reflectivity);
            }
            else {
                glUniform1i(shader.getUniformLocation("enableEnvironmentMap"), GL_FALSE);
            }

            // ---- Diffuse source: checkerboard ONLY for ground; solid color for characters ----
            if (isGround) {
                // Ground uses checkerboard texture
                glActiveTexture(GL_TEXTURE0);
                m_texture.bind(GL_TEXTURE0);                             // checkerboard
                glUniform1i(shader.getUniformLocation("colorMap"), 0);
                glUniform1i(shader.getUniformLocation("hasTexCoords"), GL_TRUE);
                glUniform1i(shader.getUniformLocation("useMaterial"),  GL_FALSE);

                // Typically no normal map for the ground plane
                glUniform1i(shader.getUniformLocation("hasNormalMap"), GL_FALSE);
            } else {
                // Characters: solid material color (no diffuse texture)
                glUniform1i(shader.getUniformLocation("hasTexCoords"), GL_FALSE);
                glUniform1i(shader.getUniformLocation("useMaterial"),  GL_TRUE);

                // Optional: only allow normal map on characters if you really want it
                if (m_enableNormalMap && mesh.hasTextureCoords()) {
                    glActiveTexture(GL_TEXTURE0 + MAX_SHADOW_LIGHTS + 1);
                    m_normalMap.bind(GL_TEXTURE0 + MAX_SHADOW_LIGHTS + 1);
                    glUniform1i(shader.getUniformLocation("normalMap"), MAX_SHADOW_LIGHTS + 1);
                    glUniform1i(shader.getUniformLocation("hasNormalMap"), GL_TRUE);
                } else {
                    glUniform1i(shader.getUniformLocation("hasNormalMap"), GL_FALSE);
                }
            }


            if (m_enablePBR) {
                glUniform1f(shader.getUniformLocation("metallic"), m_metallic);
                glUniform1f(shader.getUniformLocation("roughness"), m_roughness);
                glUniform3fv(shader.getUniformLocation("albedo"), 1, glm::value_ptr(m_albedo));
            }

            mesh.draw(shader);
        }

        // Unbind shadow textures
        for (int i = 0; i < sN; ++i) {
            glActiveTexture(GL_TEXTURE1 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    void updateCameraViewMode()
    {
        switch (m_viewMode) {
        case ViewMode::Default:
            // FPS-style camera: look from position toward front
            m_cameraTarget = m_cameraPosition + m_cameraFront;
            m_cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
            m_viewMatrix = glm::lookAt(m_cameraPosition, m_cameraTarget, m_cameraUp);
            break;

        case ViewMode::Top:
            // Static top-down camera looking at the origin
            m_cameraPosition = glm::vec3(0.0f, 10.0f, 0.01f); // small Z offset to avoid singularity
            m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
            m_cameraUp = glm::vec3(0.0f, 0.0f, -1.0f);  // look downward
            m_viewMatrix = glm::lookAt(m_cameraPosition, m_cameraTarget, m_cameraUp);
            break;

        case ViewMode::ThirdPerson:
            // Third-person camera behind the left model
        {
            glm::vec3 focusPoint = glm::vec3(-m_modelDistance * 0.5f, 1.5f, 0.0f);
            glm::vec3 offset = glm::vec3(0.0f, 2.0f, 5.0f);
            m_cameraPosition = focusPoint + offset;
            m_cameraTarget = focusPoint;
            m_cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
            m_viewMatrix = glm::lookAt(m_cameraPosition, m_cameraTarget, m_cameraUp);
        }
        break;
        }

        // Trackball overrides whichever view is selected
        if (m_trackballEnabled) {
            glm::vec3 focusPoint(0.0f, 1.0f, 0.0f);
            float radYaw = glm::radians(m_yaw);
            float radPitch = glm::radians(m_pitch);

            m_cameraPosition.x = focusPoint.x + m_distanceFromTarget * std::cos(radPitch) * std::sin(radYaw);
            m_cameraPosition.y = focusPoint.y + m_distanceFromTarget * std::sin(radPitch);
            m_cameraPosition.z = focusPoint.z + m_distanceFromTarget * std::cos(radPitch) * std::cos(radYaw);
            m_cameraTarget = focusPoint;
            m_cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
            m_viewMatrix = glm::lookAt(m_cameraPosition, m_cameraTarget, m_cameraUp);
        }
    }

    void drawWindmillDepth(const glm::mat4& lightVP)
    {
        // Collect hierarchy (local transforms) once
        std::vector<std::pair<WindmillNode*, glm::mat4>> renderData;
        m_windmill.collectRenderData(renderData);

        // shadow shader is already bound by the caller
        const GLint locLightMVP = m_shadowShader.getUniformLocation("lightMVP");

        glBindVertexArray(m_cubeVAO);

        // For every windmill instance…
        for (const glm::vec3& pos : m_windmillPositions) {
            const glm::mat4 world = glm::translate(glm::mat4(1.0f), pos);

            // …draw every node of the hierarchy
            for (auto& [node, localTransform] : renderData) {
                const glm::mat4 model    = world * localTransform * glm::scale(glm::mat4(1.0f), node->primitive.scale);
                const glm::mat4 lightMVP = lightVP * model;
                glUniformMatrix4fv(locLightMVP, 1, GL_FALSE, glm::value_ptr(lightMVP));
                glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
            }
        }

        glBindVertexArray(0);
    }




    // ---------- Events ----------

    void onScroll(const glm::dvec2& offset)
    {
        if (m_trackballEnabled) {
            m_distanceFromTarget -= static_cast<float>(offset.y) * 0.5f;
            m_distanceFromTarget = glm::clamp(m_distanceFromTarget, 1.0f, 20.0f);
        }
    }

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

        // Add to onKeyPressed method:
        if (key == GLFW_KEY_1 && !(mods & GLFW_MOD_SHIFT)) {
            shootBullet(0); // Left shoots
        }
        if (key == GLFW_KEY_2 && !(mods & GLFW_MOD_SHIFT)) {
            shootBullet(1); // Right shoots
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
        glm::dvec2 d = cursorPos - m_lastCursor;

        if (m_rotating) {
            // FPS camera look
            m_yaw += (float)d.x * m_mouseSensitivity * 10.0f;
            m_pitch -= (float)d.y * m_mouseSensitivity * 10.0f;
            m_pitch = glm::clamp(m_pitch, -89.0f, 89.0f);
            updateFrontFromYawPitch();
        }

        if (m_trackballRotating) {
            float rotSpeed = 0.25f;
            m_yaw += (float)d.x * rotSpeed;
            m_pitch += (float)d.y * rotSpeed;
            m_pitch = glm::clamp(m_pitch, -89.0f, 89.0f);
        }

        m_lastCursor = cursorPos;
    }


    void onMouseClicked(int button, int /*mods*/) {
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
            m_rotating = true; // existing FPS camera look
        if (button == GLFW_MOUSE_BUTTON_LEFT && m_trackballEnabled)
            m_trackballRotating = true; // start orbiting
    }

    void onMouseReleased(int button, int /*mods*/) {
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
            m_rotating = false;
        if (button == GLFW_MOUSE_BUTTON_LEFT)
            m_trackballRotating = false;
    }


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
    Texture m_normalMap;
    Texture m_diffuseTexture;
    bool   m_useMaterial{ true };
    bool   m_enableNormalMap{ false }; 
    bool m_enableDiffuseTexture{ false };

    // Camera
    glm::vec3 m_cameraPosition, m_cameraTarget, m_cameraUp;
    glm::vec3 m_cameraFront { 0.0f, 0.0f, -1.0f };
    float     m_yaw   { -90.0f };
    float     m_pitch {   0.0f };
    float     m_mouseSensitivity { 0.05f };
    float     m_moveSpeed { 2.0f };

    // --- Trackball Camera ---
    bool  m_trackballEnabled{ false };
    float m_distanceFromTarget{ 5.0f }; // how far the camera orbits from focus
    glm::vec2 m_trackballPrev{ 0.0f };
    bool  m_trackballRotating{ false };

    // --- Multiple Viewpoints ---
    enum class ViewMode { Default, Top, ThirdPerson };
    ViewMode m_viewMode{ ViewMode::Default };

    float m_kd { 1.0f };  // diffuse coefficient
    float m_ks { 0.25f }; // specular coefficient
    float m_shininess{ 32.0f };

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
    float m_modelDistance { 0.20f };

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
