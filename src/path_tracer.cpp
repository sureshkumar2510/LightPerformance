#include "path_tracer.h"
#include "shader.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <cstring>

// ============================================================================
// Small uniform helpers (compile-safe on g++)
// ============================================================================
namespace {
    inline GLint GetLoc(GLuint prog, const char* name) {
        return glGetUniformLocation(prog, name);
    }
    inline void Set1f(GLuint prog, const char* name, float v) {
        if (GLint l = GetLoc(prog, name); l >= 0) glUniform1f(l, v);
    }
    inline void Set1i(GLuint prog, const char* name, int v) {
        if (GLint l = GetLoc(prog, name); l >= 0) glUniform1i(l, v);
    }
    inline void Set1ui(GLuint prog, const char* name, unsigned v) {
        if (GLint l = GetLoc(prog, name); l >= 0) glUniform1ui(l, v);
    }
    inline void Set2i(GLuint prog, const char* name, int x, int y) {
        if (GLint l = GetLoc(prog, name); l >= 0) glUniform2i(l, x, y);
    }
    inline void Set3f(GLuint prog, const char* name, float x, float y, float z) {
        if (GLint l = GetLoc(prog, name); l >= 0) glUniform3f(l, x, y, z);
    }
}

// ============================================================================
// PathTracer implementation
// ============================================================================

bool PathTracer::init(int width, int height) {
    fbWidth  = width;
    fbHeight = height;

    if (!createPrograms())   return false;
    if (!createQuad())       return false;
    if (!createAccumTex(width, height)) return false;

    // Reasonable defaults
    samplesPerDispatch = 1;
    frameIndex = 0;
    return true;
}

void PathTracer::shutdown() {
    destroyAccumTex();
    destroyQuad();
    destroyPrograms();
    if (ssboTriangles) { glDeleteBuffers(1, &ssboTriangles); ssboTriangles = 0; }
    if (ssboBVH)       { glDeleteBuffers(1, &ssboBVH);       ssboBVH = 0; }
    trianglesCPU.clear();
    bvhCPU.clear();
}

void PathTracer::resize(int width, int height) {
    if (width == fbWidth && height == fbHeight) return;
    fbWidth  = width;
    fbHeight = height;
    destroyAccumTex();
    createAccumTex(width, height);
    resetAccum();
}

bool PathTracer::appendTrianglesFromFile(const std::string& path, int matId) {
    Assimp::Importer importer;
    unsigned flags = aiProcess_Triangulate
                   | aiProcess_JoinIdenticalVertices
                   | aiProcess_GenSmoothNormals
                   | aiProcess_SortByPType;

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        std::cerr << "[Assimp] " << importer.GetErrorString() << "\n";
        return false;
    }

    size_t before = trianglesCPU.size();

    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];
        if (!m->HasPositions()) continue;

        for (unsigned fi = 0; fi < m->mNumFaces; ++fi) {
            const aiFace& f = m->mFaces[fi];
            if (f.mNumIndices != 3) continue;

            const aiVector3D& a = m->mVertices[f.mIndices[0]];
            const aiVector3D& b = m->mVertices[f.mIndices[1]];
            const aiVector3D& c = m->mVertices[f.mIndices[2]];

            TriCPU tri;
            tri.p0 = { a.x, a.y, a.z };
            tri.p1 = { b.x, b.y, b.z };
            tri.p2 = { c.x, c.y, c.z };

            // Vertex normals (used for smooth gold shading; diamond uses geometric normals in shader)
            if (m->HasNormals()) {
                const aiVector3D& na = m->mNormals[f.mIndices[0]];
                const aiVector3D& nb = m->mNormals[f.mIndices[1]];
                const aiVector3D& nc = m->mNormals[f.mIndices[2]];
                tri.n0 = { na.x, na.y, na.z };
                tri.n1 = { nb.x, nb.y, nb.z };
                tri.n2 = { nc.x, nc.y, nc.z };
            } else {
                // Fallback: compute flat face normal
                glm::vec3 p0(tri.p0), p1(tri.p1), p2(tri.p2);
                glm::vec3 fn = glm::normalize(glm::cross(p1 - p0, p2 - p0));
                tri.n0 = tri.n1 = tri.n2 = fn;
            }

            tri.matId = matId;
            trianglesCPU.push_back(tri);
        }
    }

    size_t added = trianglesCPU.size() - before;
    if (added == 0) {
        std::cerr << "[PathTracer] No triangles found in: " << path << "\n";
        return false;
    }

    return true;
}

bool PathTracer::loadSceneFromFiles(const std::string& diamondPath, const std::string& colletPath) {
    trianglesCPU.clear();
    trianglesCPU.reserve(1u << 20);

    // 0 = diamond, 1 = gold
    if (!appendTrianglesFromFile(diamondPath, /*matId=*/0)) {
        std::cerr << "[PathTracer] Diamond load failed.\n";
        return false;
    }

    if (!colletPath.empty()) {
        if (!appendTrianglesFromFile(colletPath, /*matId=*/1)) {
            std::cerr << "[PathTracer] Collet load failed (continuing without collet).\n";
            // Keep diamond-only; do not fail the whole load.
        }
    }

    if (trianglesCPU.empty()) {
        std::cerr << "[PathTracer] No triangles found.\n";
        return false;
    }

    // Build BVH over tri indices, then reorder triangles to match leaf ranges
    buildBVH();
    if (!uploadGeometry()) return false;

    resetAccum();
    std::cout << "[PathTracer] Scene loaded: " << trianglesCPU.size()
              << " triangles, BVH nodes: " << bvhCPU.size() << "\n";
    return true;
}

bool PathTracer::loadSceneFromFile(const std::string& path) {
    return loadSceneFromFiles(path, "");
}

// Camera change detection → auto reset
static inline bool nearEqF(float a, float b, float eps=1e-5f){ return std::fabs(a-b) < eps; }
static inline bool nearEq3(const glm::vec3& A, const glm::vec3& B, float eps=1e-5f){
    return nearEqF(A.x,B.x,eps) && nearEqF(A.y,B.y,eps) && nearEqF(A.z,B.z,eps);
}

void PathTracer::setCamera(const glm::vec3& camPos,
                           const glm::vec3& camDir,
                           const glm::vec3& camRight,
                           const glm::vec3& camUp,
                           float fovYRadians,
                           float aspect) {
    bool changed =
        !nearEq3(uCamPos,   camPos)   ||
        !nearEq3(uCamDir,   camDir)   ||
        !nearEq3(uCamRight, camRight) ||
        !nearEq3(uCamUp,    camUp)    ||
        !nearEqF(uFovY,     fovYRadians) ||
        !nearEqF(uAspect,   aspect);

    uCamPos   = camPos;
    uCamDir   = camDir;
    uCamRight = camRight;
    uCamUp    = camUp;
    uFovY     = fovYRadians;
    uAspect   = aspect;

    if (changed) resetAccum();
}

void PathTracer::setShowCollet(bool show) {
    int v = show ? 1 : 0;
    if (showCollet != v) {
        showCollet = v;
        resetAccum();
    }
}

void PathTracer::resetAccum() {
    frameIndex = 0;

    // Clear accumulation texture to zero
    glBindTexture(GL_TEXTURE_2D, accumTex);
    std::vector<float> zero(static_cast<size_t>(fbWidth) * fbHeight * 4u, 0.0f);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fbWidth, fbHeight, GL_RGBA, GL_FLOAT, zero.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PathTracer::render() {
    dispatchCompute();
    present();
}

// ============================================================================
// GL resource management
// ============================================================================

bool PathTracer::createPrograms() {
    // Compute kernel (Phase-4: Sellmeier + dome/ring env)
    progCompute = CompileComputeProgramFromFile("shaders/pathtrace.comp");
    if (!progCompute) return false;

    // Fullscreen tonemap pass (XYZ->sRGB + exposure + ACES)
    progTonemap = CompileShaderProgramFromFiles("shaders/fullscreen.vert", "shaders/tonemap.frag");
    if (!progTonemap) return false;

    return true;
}

void PathTracer::destroyPrograms() {
    if (progCompute) { glDeleteProgram(progCompute); progCompute = 0; }
    if (progTonemap) { glDeleteProgram(progTonemap); progTonemap = 0; }
}

bool PathTracer::createQuad() {
    if (quadVao) return true;
    glGenVertexArrays(1, &quadVao);
    return quadVao != 0;
}

void PathTracer::destroyQuad() {
    if (quadVao) { glDeleteVertexArrays(1, &quadVao); quadVao = 0; }
}

bool PathTracer::createAccumTex(int w, int h) {
    glGenTextures(1, &accumTex);
    glBindTexture(GL_TEXTURE_2D, accumTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    return accumTex != 0;
}

void PathTracer::destroyAccumTex() {
    if (accumTex) { glDeleteTextures(1, &accumTex); accumTex = 0; }
}

// ============================================================================
// Geometry & BVH
// ============================================================================

bool PathTracer::uploadGeometry() {
    // GPU triangle buffer (pack to vec4 for std430 alignment)
    struct TriGPU { glm::vec4 p0, p1, p2; glm::vec4 n0, n1, n2; };
    std::vector<TriGPU> trisGPU;
    trisGPU.reserve(trianglesCPU.size());
    for (const auto& t : trianglesCPU) {
        trisGPU.push_back({
            glm::vec4(t.p0, float(t.matId)),
            glm::vec4(t.p1, 0.f),
            glm::vec4(t.p2, 0.f),
            glm::vec4(t.n0, 0.f),
            glm::vec4(t.n1, 0.f),
            glm::vec4(t.n2, 0.f)
        });
    }

    if (!ssboTriangles) glGenBuffers(1, &ssboTriangles);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboTriangles);
    glBufferData(GL_SHADER_STORAGE_BUFFER, trisGPU.size()*sizeof(TriGPU), trisGPU.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // GPU BVH nodes
    struct BVHNodeGPU { glm::vec4 bmin, bmax; glm::ivec4 child; };
    std::vector<BVHNodeGPU> nodes;
    nodes.reserve(bvhCPU.size());
    for (const auto& n : bvhCPU) {
        nodes.push_back({
            glm::vec4(n.bmin, 0.f),
            glm::vec4(n.bmax, 0.f),
            glm::ivec4(n.left, n.right, n.start, n.count)
        });
    }

    if (!ssboBVH) glGenBuffers(1, &ssboBVH);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboBVH);
    glBufferData(GL_SHADER_STORAGE_BUFFER, nodes.size()*sizeof(BVHNodeGPU), nodes.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    return true;
}

static inline float fmin3(float a, float b, float c) { return std::min(a, std::min(b, c)); }
static inline float fmax3(float a, float b, float c) { return std::max(a, std::max(b, c)); }

void PathTracer::buildBVH() {
    bvhCPU.clear();
    bvhCPU.reserve(trianglesCPU.size() * 2);

    // Build over an index array
    std::vector<int> triIdx(trianglesCPU.size());
    for (int i = 0; i < (int)trianglesCPU.size(); ++i) triIdx[i] = i;

    buildBVHRecursive(triIdx, 0, (int)triIdx.size());

    // Reorder triangle array to match the index order laid out by leaves
    std::vector<TriCPU> reordered(trianglesCPU.size());
    for (int i = 0; i < (int)triIdx.size(); ++i) {
        reordered[i] = trianglesCPU[triIdx[i]];
    }
    trianglesCPU.swap(reordered);

    // After reordering, leaf ranges [start,count] now directly index tris[]
    // so uploadGeometry() can pack contiguous ranges correctly.
}

int PathTracer::buildBVHRecursive(std::vector<int>& triIdx, int begin, int end) {
    // Compute bounds & centroid bounds
    glm::vec3 bmin( std::numeric_limits<float>::max());
    glm::vec3 bmax(-std::numeric_limits<float>::max());
    glm::vec3 cmin = bmin, cmax = bmax;

    for (int i = begin; i < end; ++i) {
        const auto& t = trianglesCPU[triIdx[i]];
        glm::vec3 pmin(
            fmin3(t.p0.x, t.p1.x, t.p2.x),
            fmin3(t.p0.y, t.p1.y, t.p2.y),
            fmin3(t.p0.z, t.p1.z, t.p2.z)
        );
        glm::vec3 pmax(
            fmax3(t.p0.x, t.p1.x, t.p2.x),
            fmax3(t.p0.y, t.p1.y, t.p2.y),
            fmax3(t.p0.z, t.p1.z, t.p2.z)
        );
        bmin = glm::min(bmin, pmin);
        bmax = glm::max(bmax, pmax);

        glm::vec3 c = (t.p0 + t.p1 + t.p2) / 3.0f;
        cmin = glm::min(cmin, c);
        cmax = glm::max(cmax, c);
    }

    int nodeIdx = (int)bvhCPU.size();
    bvhCPU.push_back(BVHNodeCPU{ bmin, -1, bmax, -1, begin, end - begin });

    // Leaf?
    const int leafThreshold = 8;
    if (end - begin <= leafThreshold) {
        return nodeIdx;
    }

    // Largest centroid axis
    glm::vec3 diag = cmax - cmin;
    int axis = 0;
    if (diag.y > diag.x && diag.y > diag.z) axis = 1;
    else if (diag.z > diag.x && diag.z > diag.y) axis = 2;

    float mid = 0.5f * (cmin[axis] + cmax[axis]);

    // Partition by centroid position
    auto midIt = std::partition(triIdx.begin() + begin, triIdx.begin() + end,
        [&](int idx){
            const auto& T = trianglesCPU[idx];
            glm::vec3 c = (T.p0 + T.p1 + T.p2) / 3.0f;
            return c[axis] < mid;
        });

    int midIdx = (int)(midIt - triIdx.begin());
    if (midIdx == begin || midIdx == end) {
        midIdx = begin + (end - begin) / 2;
    }

    int left  = buildBVHRecursive(triIdx, begin, midIdx);
    int right = buildBVHRecursive(triIdx, midIdx, end);

    // Interior node
    bvhCPU[nodeIdx].left  = left;
    bvhCPU[nodeIdx].right = right;
    bvhCPU[nodeIdx].start = -1;
    bvhCPU[nodeIdx].count = 0;

    return nodeIdx;
}

// ============================================================================
// Render passes
// ============================================================================

// Simple change detector for the enclosure lighting → reset once if changed
struct LightingSnapshot {
    glm::vec3 domeAxis;
    glm::vec3 envBase;
    glm::vec3 capRadiance;
    float     capAngle;

    glm::vec3 ringRad0;
    float     ringAng0, ringSig0;
    glm::vec3 ringRad1;
    float     ringAng1, ringSig1;

    glm::vec3 hotRad;
    float     hotAng, hotSig;

    float     absorb_mm;
};
static inline bool diff3(const glm::vec3& a, const glm::vec3& b, float eps=1e-6f){
    return (std::fabs(a.x-b.x)>eps) || (std::fabs(a.y-b.y)>eps) || (std::fabs(a.z-b.z)>eps);
}

void PathTracer::dispatchCompute() {
    glUseProgram(progCompute);

    // ------------------------------------------------------------------------
    // 1) Build current lighting snapshot (the values you intend to send)
    //    You can expose these as UI later; for now, static tuned defaults.
    // ------------------------------------------------------------------------
    LightingSnapshot cur{};
    cur.domeAxis   = glm::vec3(0.0f, 0.0f, 1.0f);
    cur.envBase    = glm::vec3(0.01f);
    cur.capRadiance= glm::vec3(5.0f);
    cur.capAngle   = glm::radians(55.0f);

    cur.ringRad0 = glm::vec3(8.0f);
    cur.ringAng0 = glm::radians(45.0f);
    cur.ringSig0 = glm::radians(8.0f);

    cur.ringRad1 = glm::vec3(6.0f);
    cur.ringAng1 = glm::radians(65.0f);
    cur.ringSig1 = glm::radians(10.0f);

    cur.hotRad = glm::vec3(12.0f);
    cur.hotAng = glm::radians(6.0f);
    cur.hotSig = glm::radians(3.0f);

    cur.absorb_mm = 0.0f; // optional Beer–Lambert (0 = clear)

    // Compare with previous; if any change → reset accumulation
    static LightingSnapshot prev{};
    static bool prevInit = false;

    bool changed = !prevInit
                 || diff3(cur.domeAxis,    prev.domeAxis)
                 || diff3(cur.envBase,     prev.envBase)
                 || diff3(cur.capRadiance, prev.capRadiance)
                 || std::fabs(cur.capAngle - prev.capAngle) > 1e-6f
                 || diff3(cur.ringRad0,    prev.ringRad0)
                 || std::fabs(cur.ringAng0 - prev.ringAng0) > 1e-6f
                 || std::fabs(cur.ringSig0 - prev.ringSig0) > 1e-6f
                 || diff3(cur.ringRad1,    prev.ringRad1)
                 || std::fabs(cur.ringAng1 - prev.ringAng1) > 1e-6f
                 || std::fabs(cur.ringSig1 - prev.ringSig1) > 1e-6f
                 || diff3(cur.hotRad,      prev.hotRad)
                 || std::fabs(cur.hotAng - prev.hotAng) > 1e-6f
                 || std::fabs(cur.hotSig - prev.hotSig) > 1e-6f
                 || std::fabs(cur.absorb_mm - prev.absorb_mm) > 1e-6f;

    if (changed) {
        resetAccum();
        prev     = cur;
        prevInit = true;
    }

    // ------------------------------------------------------------------------
    // 2) Bind accumulation image & SSBOs
    // ------------------------------------------------------------------------
    glBindImageTexture(0, accumTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboTriangles);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboBVH);

    // ------------------------------------------------------------------------
    // 3) Push uniforms (camera, integrator, lighting)
    // ------------------------------------------------------------------------
    Set3f (progCompute, "uCamPos",   uCamPos.x,   uCamPos.y,   uCamPos.z);
    Set3f (progCompute, "uCamDir",   uCamDir.x,   uCamDir.y,   uCamDir.z);
    Set3f (progCompute, "uCamRight", uCamRight.x, uCamRight.y, uCamRight.z);
    Set3f (progCompute, "uCamUp",    uCamUp.x,    uCamUp.y,    uCamUp.z);
    Set1f (progCompute, "uFovY",     uFovY);
    Set1f (progCompute, "uAspect",   uAspect);

    Set1ui(progCompute, "uFrameIndex", frameIndex);
    Set1i (progCompute, "uSamplesPerDispatch", samplesPerDispatch);
    Set2i (progCompute, "uResolution", fbWidth, fbHeight);

    Set1i (progCompute, "uShowCollet", showCollet);

    // Gold appearance (polished jewellery finish)
    Set1f (progCompute, "uGoldRoughness", 0.06f);

    Set1i (progCompute, "uMaxDepth",  32);
    Set1i (progCompute, "uRRStart",   8);

    // Enclosure lighting (dome + two rings + hotspot)
    Set3f (progCompute, "uDomeAxis",        cur.domeAxis.x, cur.domeAxis.y, cur.domeAxis.z);
    Set3f (progCompute, "uEnvBase",         cur.envBase.x,  cur.envBase.y,  cur.envBase.z);
    Set3f (progCompute, "uCapRadiance",     cur.capRadiance.x, cur.capRadiance.y, cur.capRadiance.z);
    Set1f (progCompute, "uCapAngle",        cur.capAngle);

    Set3f (progCompute, "uRingRadiance0",   cur.ringRad0.x, cur.ringRad0.y, cur.ringRad0.z);
    Set1f (progCompute, "uRingAngle0",      cur.ringAng0);
    Set1f (progCompute, "uRingSigma0",      cur.ringSig0);

    Set3f (progCompute, "uRingRadiance1",   cur.ringRad1.x, cur.ringRad1.y, cur.ringRad1.z);
    Set1f (progCompute, "uRingAngle1",      cur.ringAng1);
    Set1f (progCompute, "uRingSigma1",      cur.ringSig1);

    Set3f (progCompute, "uHotRadiance",     cur.hotRad.x, cur.hotRad.y, cur.hotRad.z);
    Set1f (progCompute, "uHotAngle",        cur.hotAng);
    Set1f (progCompute, "uHotSigma",        cur.hotSig);

    // Optional absorption (Beer–Lambert inside the diamond; 0 = off)
    Set1f (progCompute, "uAbsorb_mm",       cur.absorb_mm);

    // ------------------------------------------------------------------------
    // 4) Dispatch compute
    // ------------------------------------------------------------------------
    const int gx = (fbWidth  + 7) / 8;
    const int gy = (fbHeight + 7) / 8;
    glDispatchCompute(gx, gy, 1);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Increment progressive frame index
    frameIndex++;
}

void PathTracer::present() {
    glUseProgram(progTonemap);

    // Bind accumulated XYZ
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accumTex);
    if (GLint loc = GetLoc(progTonemap, "uAccumTex"); loc >= 0) glUniform1i(loc, 0);

    // Exposure (post-process; no reset needed when it changes)
    Set1f(progTonemap, "uExposure", 1.8f);

    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}
