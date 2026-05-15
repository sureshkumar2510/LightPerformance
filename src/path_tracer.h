#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>

// Minimal path tracer: BVH + triangle SSBO + accumulation + fullscreen display.
class PathTracer {
public:
    // Lifecycle
    bool init(int width, int height);
    void shutdown();

    // Resize accumulation and output
    void resize(int width, int height);

    // Load scene from mesh file(s) (Assimp), build BVH on CPU, upload to SSBOs
    // - diamondPath: required
    // - colletPath: optional (empty string -> diamond-only)
    bool loadSceneFromFiles(const std::string& diamondPath, const std::string& colletPath);

    // Compatibility wrapper (diamond-only)
    bool loadSceneFromFile(const std::string& path);

    // Camera setup (pinhole camera)
    // camPos: world position of camera
    // camDir: normalized forward
    // camRight, camUp: orthonormal basis
    // fovYRadians, aspect
    void setCamera(const glm::vec3& camPos,
                   const glm::vec3& camDir,
                   const glm::vec3& camRight,
                   const glm::vec3& camUp,
                   float fovYRadians,
                   float aspect);

    // Toggle visibility of the gold collet triangles in the compute renderer
    void setShowCollet(bool show);

    // Reset progressive accumulation (call when camera or scene changes)
    void resetAccum();

    // One frame render (dispatch compute), then present (fullscreen blit)
    void render();

    // Toggle real-time sampling rate (samples per-dispatch)
    void setSamplesPerDispatch(int spp) { samplesPerDispatch = spp; }

private:
    // GPU resources
    GLuint progCompute = 0;      // pathtrace.comp
    GLuint progTonemap = 0;      // fullscreen.vert + tonemap.frag
    GLuint quadVao = 0;

    // Accumulation target (RGBA32F), read-write in compute, sampled in tonemap
    GLuint accumTex = 0;
    int fbWidth = 0, fbHeight = 0;

    // SSBOs for geometry and BVH
    GLuint ssboTriangles = 0;
    GLuint ssboBVH = 0;

    // Scene data kept on CPU for building BVH
    // matId: 0 = diamond, 1 = gold
    struct TriCPU {
        glm::vec3 p0, p1, p2;
        glm::vec3 n0, n1, n2; // vertex normals (used for smooth shading on gold)
        int matId = 0;
    };
    struct BVHNodeCPU {
        glm::vec3 bmin; int left;   // left child index or -1
        glm::vec3 bmax; int right;  // right child index or -1
        int start; int count;       // triangle range for leaf
    };
    std::vector<TriCPU> trianglesCPU;
    std::vector<BVHNodeCPU> bvhCPU;

    // Camera uniforms
    glm::vec3 uCamPos{0,0,5};
    glm::vec3 uCamDir{0,0,-1};
    glm::vec3 uCamRight{1,0,0};
    glm::vec3 uCamUp{0,1,0};
    float uFovY = 45.0f * 3.14159265f / 180.0f;
    float uAspect = 16.0f/9.0f;

    // Accumulation frame index
    unsigned int frameIndex = 0;

    int samplesPerDispatch = 1;  // for real-time, keep small initially

    // Scene toggles / material controls
    int showCollet = 1; // 1 = show, 0 = hide gold triangles

private:
    bool createPrograms();      // compile/link shaders
    void destroyPrograms();

    bool createQuad();
    void destroyQuad();

    bool createAccumTex(int w, int h);
    void destroyAccumTex();

    bool appendTrianglesFromFile(const std::string& path, int matId);
    bool uploadGeometry();      // trianglesCPU -> SSBO, bvhCPU -> SSBO

    // BVH builder (median split on largest axis)
    void buildBVH();
    int  buildBVHRecursive(std::vector<int>& triIdx, int begin, int end);

    // Utilities
    void dispatchCompute();
    void present();
};
