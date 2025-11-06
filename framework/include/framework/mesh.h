#pragma once
#include "image.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texCoord; 
	glm::vec3 tangent;
	[[nodiscard]] constexpr bool operator==(const Vertex&) const noexcept = default;
};

struct Material {
	glm::vec3 kd; 
	glm::vec3 ks{ 0.0f };
	float shininess{ 1.0f };
	float transparency{ 1.0f };	
	std::shared_ptr<Image> kdTexture;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<glm::uvec3> triangles;

	Material material;
};

struct LoadMeshSettings {
	bool normalizeVertexPositions { false };
	bool cacheVertices { true };
};

[[nodiscard]] std::vector<Mesh> loadMesh(const std::filesystem::path& file, const LoadMeshSettings& settings = {});
[[nodiscard]] Mesh mergeMeshes(std::span<const Mesh> meshes);
void meshFlipX(Mesh& mesh);
void meshFlipY(Mesh& mesh);
void meshFlipZ(Mesh& mesh);