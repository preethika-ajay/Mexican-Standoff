#pragma once

#include <framework/disable_all_warnings.h>
#include <framework/mesh.h>
#include <framework/shader.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

#include <exception>
#include <filesystem>
#include <framework/opengl_includes.h>

struct MeshLoadingException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct GPUMaterial {
    GPUMaterial(const Material& material);

    alignas(16) glm::vec3 kd{ 1.0f };
	alignas(16) glm::vec3 ks{ 0.0f };
	float shininess{ 1.0f };
	float transparency{ 1.0f };
};

class GPUMesh {
public:
    GPUMesh(const Mesh& cpuMesh);
    
    GPUMesh(const GPUMesh&) = delete;
    GPUMesh(GPUMesh&&);
    ~GPUMesh();
   
    static std::vector<GPUMesh> loadMeshGPU(std::filesystem::path filePath, bool normalize = false);
   
    GPUMesh& operator=(const GPUMesh&) = delete;
    GPUMesh& operator=(GPUMesh&&);

    bool hasTextureCoords() const;
   
    void draw(const Shader& drawingShader);

private:
    void moveInto(GPUMesh&&);
    void freeGpuMemory();

private:
    static constexpr GLuint INVALID = 0xFFFFFFFF;

    GLsizei m_numIndices { 0 };
    bool m_hasTextureCoords { false };
    GLuint m_ibo { INVALID };
    GLuint m_vbo { INVALID };
    GLuint m_vao { INVALID };
    GLuint m_uboMaterial { INVALID };
};
