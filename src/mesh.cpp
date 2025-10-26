#include "mesh.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <iostream>
#include <vector>
#include <glm/geometric.hpp>
#include <unordered_map>



// Recompute smooth vertex normals (grouped by exact position).
// This averages the accumulated face normals across all vertices that share
// the same 3D position, which "merges" shading across split vertices that
// OBJ often creates (e.g., by texcoord/normal indices).
static void recomputeSmoothNormals(Mesh& m)
{
    // Zero out normals
    for (auto& v : m.vertices)
        v.normal = glm::vec3(0.0f);

    // Accumulate (unit) face normals per vertex
    for (const glm::uvec3& tri : m.triangles) {
        const glm::vec3& p0 = m.vertices[tri.x].position;
        const glm::vec3& p1 = m.vertices[tri.y].position;
        const glm::vec3& p2 = m.vertices[tri.z].position;

        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        float len2 = glm::dot(n, n);
        if (len2 > 0.0f) n /= std::sqrt(len2); // normalize

        m.vertices[tri.x].normal += n;
        m.vertices[tri.y].normal += n;
        m.vertices[tri.z].normal += n;
    }

    // Average across duplicate positions (OBJ often duplicates verts)
    struct Vec3Hash {
        size_t operator()(const glm::vec3& v) const noexcept {
            auto h1 = std::hash<float>{}(v.x);
            auto h2 = std::hash<float>{}(v.y);
            auto h3 = std::hash<float>{}(v.z);
            // simple mix
            return ((h1 * 1315423911u) ^ (h2 + 0x9e3779b9 + (h1<<6) + (h1>>2))) ^ h3;
        }
    };
    struct Vec3Eq {
        bool operator()(const glm::vec3& a, const glm::vec3& b) const noexcept {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }
    };

    std::unordered_map<glm::vec3, glm::vec3, Vec3Hash, Vec3Eq> sumByPos;
    std::unordered_map<glm::vec3, int,      Vec3Hash, Vec3Eq> countByPos;

    for (const auto& v : m.vertices) {
        auto it = sumByPos.find(v.position);
        if (it == sumByPos.end()) {
            sumByPos.emplace(v.position, v.normal);
            countByPos.emplace(v.position, 1);
        } else {
            it->second += v.normal;
            countByPos[v.position] += 1;
        }
    }

    for (auto& v : m.vertices) {
        glm::vec3 sum = sumByPos[v.position];
        int cnt = countByPos[v.position];
        glm::vec3 avg = (cnt > 0) ? (sum / float(cnt)) : v.normal;
        float l2 = glm::dot(avg, avg);
        if (l2 > 0.0f) avg /= std::sqrt(l2);
        v.normal = avg;
    }
}
GPUMaterial::GPUMaterial(const Material& material) :
    kd(material.kd),
    ks(material.ks),
    shininess(material.shininess),
    transparency(material.transparency)
{}

GPUMesh::GPUMesh(const Mesh& cpuMesh)
{
    // Create uniform buffer to store mesh material (https://learnopengl.com/Advanced-OpenGL/Advanced-GLSL)
    GPUMaterial gpuMaterial(cpuMesh.material);
    glGenBuffers(1, &m_uboMaterial);
    glBindBuffer(GL_UNIFORM_BUFFER, m_uboMaterial);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(GPUMaterial), &gpuMaterial, GL_STATIC_READ);

    // Figure out if this mesh has texture coordinates
    m_hasTextureCoords = static_cast<bool>(cpuMesh.material.kdTexture);

    // Create VAO and bind it so subsequent creations of VBO and IBO are bound to this VAO
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // Create vertex buffer object (VBO)
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.vertices.size() * sizeof(decltype(cpuMesh.vertices)::value_type)), cpuMesh.vertices.data(), GL_STATIC_DRAW);

    // Create index buffer object (IBO)
    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.triangles.size() * sizeof(decltype(cpuMesh.triangles)::value_type)), cpuMesh.triangles.data(), GL_STATIC_DRAW);

    // Tell OpenGL that we will be using vertex attributes 0, 1 and 2.
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    // We tell OpenGL what each vertex looks like and how they are mapped to the shader (location = ...).
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
    // Reuse all attributes for each instance
    glVertexAttribDivisor(0, 0);
    glVertexAttribDivisor(1, 0);
    glVertexAttribDivisor(2, 0);

    // Each triangle has 3 vertices.
    m_numIndices = static_cast<GLsizei>(3 * cpuMesh.triangles.size());
}

GPUMesh::GPUMesh(GPUMesh&& other)
{
    moveInto(std::move(other));
}

GPUMesh::~GPUMesh()
{
    freeGpuMemory();
}

GPUMesh& GPUMesh::operator=(GPUMesh&& other)
{
    moveInto(std::move(other));
    return *this;
}

std::vector<GPUMesh> GPUMesh::loadMeshGPU(std::filesystem::path filePath, bool normalize) {
    if (!std::filesystem::exists(filePath))
        throw MeshLoadingException(fmt::format("File {} does not exist", filePath.string().c_str()));

    // Load all sub-meshes on CPU
    std::vector<Mesh> subMeshes = loadMesh(filePath, { .normalizeVertexPositions = normalize });
    std::vector<GPUMesh> gpuMeshes;
    gpuMeshes.reserve(subMeshes.size());

    for (Mesh meshCopy : subMeshes) {
        // Force smooth shading regardless of OBJ-provided normals
        recomputeSmoothNormals(meshCopy);
        gpuMeshes.emplace_back(meshCopy);
    }

    return gpuMeshes;
}

bool GPUMesh::hasTextureCoords() const
{
    return m_hasTextureCoords;
}

void GPUMesh::draw(const Shader& drawingShader)
{
    // Bind material data uniform (we assume that the uniform buffer objects is always called 'Material')
    // Yes, we could define the binding inside the shader itself, but that would break on OpenGL versions below 4.2
    drawingShader.bindUniformBlock("Material", 0, m_uboMaterial);
    
    // Draw the mesh's triangles
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_numIndices, GL_UNSIGNED_INT, nullptr);
}

void GPUMesh::moveInto(GPUMesh&& other)
{
    freeGpuMemory();
    m_numIndices = other.m_numIndices;
    m_hasTextureCoords = other.m_hasTextureCoords;
    m_ibo = other.m_ibo;
    m_vbo = other.m_vbo;
    m_vao = other.m_vao;
    m_uboMaterial = other.m_uboMaterial;

    other.m_numIndices = 0;
    other.m_hasTextureCoords = other.m_hasTextureCoords;
    other.m_ibo = INVALID;
    other.m_vbo = INVALID;
    other.m_vao = INVALID;
    other.m_uboMaterial = INVALID;
}

void GPUMesh::freeGpuMemory()
{
    if (m_vao != INVALID)
        glDeleteVertexArrays(1, &m_vao);
    if (m_vbo != INVALID)
        glDeleteBuffers(1, &m_vbo);
    if (m_ibo != INVALID)
        glDeleteBuffers(1, &m_ibo);
    if (m_uboMaterial != INVALID)
        glDeleteBuffers(1, &m_uboMaterial);
}
