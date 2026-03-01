# Western Showdown — OpenGL Tech Demo

*Western Showdown* is a real-time OpenGL 4.1 tech demo presenting a Mexican standoff / desert duel between two gunslingers.

The project demonstrates multiple rendering and animation techniques that can be toggled through an ImGui interface.

Implemented features include:

- Physically Based Rendering (PBR) and Blinn–Phong hybrid shading
- Environment mapping and normal mapping
- Hierarchical animation (procedural windmill)
- Smooth Bézier camera and bullet paths
- Particle system (muzzle flash and explosions)
- Multi-light shadow mapping with PCF filtering

All effects run in real time within an OpenGL rendering pipeline.

---

# Implemented Features

## Multiple Viewpoints

The camera system supports three viewpoints selectable through ImGui.

### Views

- **Default View** – first-person perspective  
- **Top View** – overhead camera (`y = 10.0`)  
- **Third-Person View** – camera follows the player

View matrices are computed using:

```cpp
glm::lookAt(position, target, up);
```

Trackball mode allows manual navigation using mouse pitch and yaw.

---

## Physically Based Rendering (PBR)

Hybrid shading combining:

- Microfacet BRDF
- Blinn–Phong lighting
- Fresnel reflectance interpolation

Supports metallic and diffuse material behavior.

---

## Material Textures

Surface interaction parameters:

- Diffuse coefficient (`kd`)
- Specular coefficient (`ks`)
- Shininess
- Roughness

---

## Normal Mapping and Environment Mapping

### Normal Mapping
Perturbs fragment normals using normal maps.

### Environment Mapping

```glsl
R = reflect(I, N);
```

Cubemaps simulate environment reflections.

---

## Hierarchical Animation — Windmill

Parent–child transformation hierarchy:

- Translation
- Rotation
- Scale

Blade rotation speed depends on wind intensity.

---

## Bézier Camera and Bullet Paths

Smooth motion implemented using cubic Bézier curves.

Features:
- Arc-length reparameterization
- Constant-speed motion
- Smooth trajectories

---

## Shadow Mapping

Real-time shadow generation:

- Per-light depth maps
- PCF filtering
- Adaptive shadow bias

---

## Particle System

Event-driven emitters generate:

- Muzzle flashes
- Explosions
- Smoke trails

Implementation:
- CPU-driven particles
- Rendered as `GL_POINTS`
- Attributes: position, velocity, lifetime, alpha decay

---

## Multi-Light Shadows

- Individual shadow FBO per light
- Aggregated lighting contributions
- Supports up to **16 lights**

---

## Environment-Based Shadows

Cubemap lighting influences ground shadow orientation.

---

## Collision Detection

Bounding-sphere intersection tests trigger:

- Particle explosions
- Animation events
- Gameplay feedback

---

# Build Instructions

## Requirements

- OpenGL ≥ 4.1
- C++17
- GLFW
- GLAD
- GLM
- ImGui

## Build

```bash
mkdir build
cd build
cmake ..
make
```

Run:

```bash
./WesternShowdown
```

