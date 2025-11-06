#include "mesh.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <iostream>
#include <vector>
#include <glm/geometric.hpp>
#include <unordered_map>

static void recomputeSmoothNormals(Mesh& m)
{
    for (auto& v : m.vertices)
        v.normal = glm::vec3(0.0f);
   
    for (const glm::uvec3& tri : m.triangles) {
        const glm::vec3& p0 = m.vertices[tri.x].position;
        const glm::vec3& p1 = m.vertices[tri.y].position;
        const glm::vec3& p2 = m.vertices[tri.z].position;

        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        float len2 = glm::dot(n, n);
        if (len2 > 0.0f) n /= std::sqrt(len2); 
        m.vertices[tri.x].normal += n;
        m.vertices[tri.y].normal += n;
        m.vertices[tri.z].normal += n;
    }

    struct Vec3Hash {
        size_t operator()(const glm::vec3& v) const noexcept {
            auto h1 = std::hash<float>{}(v.x);
            auto h2 = std::hash<float>{}(v.y);
            auto h3 = std::hash<float>{}(v.z);            
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

static void generatePlanarUVs(Mesh& m)
{
    if (m.vertices.empty()) return;
   
    glm::vec3 minPos = m.vertices[0].position;
    glm::vec3 maxPos = m.vertices[0].position;

    for (const auto& v : m.vertices) {
        minPos = glm::min(minPos, v.position);
        maxPos = glm::max(maxPos, v.position);
    }

    glm::vec3 size = maxPos - minPos;
    float maxSize = std::max(std::max(size.x, size.y), size.z);
    if (maxSize < 1e-6f) maxSize = 1.0f;
    
    for (auto& v : m.vertices) {
        v.texCoord.x = (v.position.x - minPos.x) / maxSize;
        v.texCoord.y = (v.position.z - minPos.z) / maxSize;
    }

    std::cout << "Generated planar UVs for mesh\n";
}

GPUMesh::GPUMesh(const Mesh& cpuMesh)
{    
    GPUMaterial gpuMaterial(cpuMesh.material);
    glGenBuffers(1, &m_uboMaterial);
    glBindBuffer(GL_UNIFORM_BUFFER, m_uboMaterial);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(GPUMaterial), &gpuMaterial, GL_STATIC_READ);
   
    m_hasTextureCoords = false;
    if (!cpuMesh.vertices.empty()) {        
        for (const auto& v : cpuMesh.vertices) {
            if (v.texCoord.x != 0.0f || v.texCoord.y != 0.0f) {
                m_hasTextureCoords = true;
                break;
            }
        }
        if (!m_hasTextureCoords && cpuMesh.vertices.size() > 1) {
            glm::vec2 first = cpuMesh.vertices[0].texCoord;
            for (size_t i = 1; i < cpuMesh.vertices.size(); ++i) {
                if (cpuMesh.vertices[i].texCoord != first) {
                    m_hasTextureCoords = true;
                    break;
                }
            }
        }
    }

    std::cout << "Mesh has " << cpuMesh.vertices.size() << " vertices, hasTextureCoords: "
        << (m_hasTextureCoords ? "YES" : "NO") << std::endl;

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.vertices.size() * sizeof(decltype(cpuMesh.vertices)::value_type)), cpuMesh.vertices.data(), GL_STATIC_DRAW);
    
    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.triangles.size() * sizeof(decltype(cpuMesh.triangles)::value_type)), cpuMesh.triangles.data(), GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
    
    glVertexAttribDivisor(0, 0);
    glVertexAttribDivisor(1, 0);
    glVertexAttribDivisor(2, 0);
    glVertexAttribDivisor(3, 0);

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

static void computeTangents(Mesh& m)
{    
    std::vector<glm::vec3> tangents(m.vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(m.vertices.size(), glm::vec3(0.0f));
   
    for (const glm::uvec3& tri : m.triangles) {
        const Vertex& v0 = m.vertices[tri.x];
        const Vertex& v1 = m.vertices[tri.y];
        const Vertex& v2 = m.vertices[tri.z];

        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;

        glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

        float f = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(f) < 1e-6f) f = 1.0f; 
        f = 1.0f / f;

        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        
        tangents[tri.x] += tangent;
        tangents[tri.y] += tangent;
        tangents[tri.z] += tangent;
    }
    
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const glm::vec3& n = m.vertices[i].normal;
        glm::vec3 t = tangents[i];

        t = t - n * glm::dot(n, t);
        
        float len = glm::length(t);
        if (len > 1e-6f) {
            t /= len;
        }
        else {            
            t = glm::vec3(1, 0, 0);
            if (std::abs(glm::dot(n, t)) > 0.9f)
                t = glm::vec3(0, 1, 0);
            t = glm::normalize(t - n * glm::dot(n, t));
        }

        m.vertices[i].tangent = t;
    }
}

std::vector<GPUMesh> GPUMesh::loadMeshGPU(std::filesystem::path filePath, bool normalize) {
    if (!std::filesystem::exists(filePath))
        throw MeshLoadingException(fmt::format("File {} does not exist", filePath.string().c_str()));

    std::vector<Mesh> subMeshes = loadMesh(filePath, { .normalizeVertexPositions = normalize });
    std::vector<GPUMesh> gpuMeshes;
    gpuMeshes.reserve(subMeshes.size());

    for (Mesh meshCopy : subMeshes) {
        recomputeSmoothNormals(meshCopy);
        generatePlanarUVs(meshCopy);  
        computeTangents(meshCopy);
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
    drawingShader.bindUniformBlock("Material", 0, m_uboMaterial);
       
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
