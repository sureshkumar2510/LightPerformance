#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cfloat>
#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "shader.h"
#include "path_tracer.h"

// -----------------------------
// Where to set model path:
// -----------------------------
static const char* kDefaultDiamondPath = "assets/models/diamond.stl";
static const char* kDefaultColletPath  = "assets/models/collet_gold.stl";
static bool gUsePathTracer = false;
static bool gPrevP = false, gPrevV = false, gPrevR = false, gPrevC = false;
static bool gCameraDirty = false;
static bool gPrevSpace = false;

// -----------------------------
// CAD-like camera state
// -----------------------------
struct Camera {
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float distance = 3.0f;
    float yaw = 0.0f;     // radians
    float pitch = 0.0f;   // radians
    float fovY = glm::radians(45.0f);

    glm::mat4 view() const {
        // Spherical coords around target
        float cp = std::cos(pitch);
        glm::vec3 eye;
        eye.x = target.x + distance * cp * std::cos(yaw);
        eye.y = target.y + distance * std::sin(pitch);
        eye.z = target.z + distance * cp * std::sin(yaw);
        return glm::lookAt(eye, target, glm::vec3(0,1,0));
    }
};

static Camera gCam;
static bool gLMB = false;
static bool gMMB = false;
static bool gShift = false;
static double gLastX = 0.0, gLastY = 0.0;

// For panning sensitivity based on view distance
static float PanScale(float dist) {
    return std::max(0.0005f * dist, 0.00005f);
}

static void KeyCallback(GLFWwindow* w, int key, int sc, int action, int mods) {
    (void)w; (void)sc;
    if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
        if (action == GLFW_PRESS) gShift = true;
        if (action == GLFW_RELEASE) gShift = false;
    }
}

// Reset camera to "top view" (looking straight down onto the table).
// Assumes the diamond is modeled with table facing +Y (common in datasets).
static void SetTopView()
{
    // Keep target & distance as-is (or refresh by re-framing if you prefer).
    // Here we reframe to ensure the model is centered and sized appropriately.
    // Requires you to have mesh.bbMin / mesh.bbMax in scope when called.
    // If not available where you call this, move the FitCameraToBounds call there.

    // Orientation: looking down -Y toward the target (top view).
    // Our camera math uses yaw (around Y) and pitch (elevation).
    // pitch = -90 deg gives a true orthogonal top-down; clamp a bit to avoid singularities.
    gCam.yaw   = -glm::radians(89.0f);
    //gCam.pitch = -glm::radians(89.0f); // avoid exact -90 to keep stable "up"
    gCam.pitch = 0.0f;
    gCameraDirty = true;
}

static void MouseButtonCallback(GLFWwindow* w, int button, int action, int mods) {
    (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT) gLMB = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) gMMB = (action == GLFW_PRESS);
    glfwGetCursorPos(w, &gLastX, &gLastY);
}

static void CursorPosCallback(GLFWwindow* w, double x, double y) {
    double dx = x - gLastX;
    double dy = y - gLastY;
    gLastX = x; gLastY = y;

    if (!(gLMB || gMMB)) return;

    // Pan: MMB OR Shift+LMB
    if (gMMB || (gShift && gLMB)) {
        int width, height;
        glfwGetFramebufferSize(w, &width, &height);
        float sx = (width  > 0) ? (float)dx / (float)width : 0.0f;
        float sy = (height > 0) ? (float)dy / (float)height : 0.0f;

        // Compute right/up from view matrix
        glm::mat4 V = gCam.view();
        glm::vec3 right = glm::vec3(V[0][0], V[1][0], V[2][0]);
        glm::vec3 up    = glm::vec3(V[0][1], V[1][1], V[2][1]);

        float k = PanScale(gCam.distance);
        gCam.target += (-right * sx + up * sy) * (k * 2.0f);
        gCameraDirty = true;
        return;
    }

    // Orbit: LMB
    if (gLMB) {
        const float rotSpeed = 0.01f;
        gCam.yaw   += (float)dx * rotSpeed;
        gCam.pitch += (float)-dy * rotSpeed;

        // clamp pitch
        float limit = glm::radians(89.0f);
        gCam.pitch = std::clamp(gCam.pitch, -limit, limit);
    }
}

static void ScrollCallback(GLFWwindow* w, double xoff, double yoff) {
    (void)w; (void)xoff;
    // Zoom (dolly)
    float zoomFactor = std::pow(1.1f, (float)-yoff);
    gCam.distance = std::clamp(gCam.distance * zoomFactor, 0.01f, 1e6f);
    gCameraDirty = true;
}

// -----------------------------
// Mesh GPU data
// -----------------------------
struct Vertex {
    glm::vec3 pos;
    glm::vec3 nrm;
};

struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
    glm::vec3 bbMin{0}, bbMax{0};
};

// -----------------------------
// Assimp loader: combines all meshes into ONE big VBO/EBO
// -----------------------------
// We use common post-process flags like triangulation, joining identical vertices, and cache locality improvements. [6](https://documentation.help/ASSIMP-Command-line-tools/common.html)[7](https://deepwiki.com/assimp/assimp/3.5-post-processing)
static GpuMesh LoadMeshToGpu(const std::string& path) {
    Assimp::Importer importer;

    unsigned int flags =
        aiProcess_Triangulate |                 // make sure faces are triangles [6](https://documentation.help/ASSIMP-Command-line-tools/common.html)
        aiProcess_JoinIdenticalVertices |        // helps reduce duplication and optimize indices [6](https://documentation.help/ASSIMP-Command-line-tools/common.html)
        aiProcess_GenSmoothNormals |             // generate normals if missing [6](https://documentation.help/ASSIMP-Command-line-tools/common.html)
        aiProcess_ImproveCacheLocality |         // reorder for vertex cache locality [6](https://documentation.help/ASSIMP-Command-line-tools/common.html)
        aiProcess_SortByPType;                   // keep triangles clean [6](https://documentation.help/ASSIMP-Command-line-tools/common.html)

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        throw std::runtime_error(std::string("Assimp load failed: ") + importer.GetErrorString());
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    glm::vec3 bbMin( FLT_MAX), bbMax(-FLT_MAX);

    vertices.reserve(1000000);
    indices.reserve(3000000);

    auto expandBB = [&](const glm::vec3& p) {
        bbMin = glm::min(bbMin, p);
        bbMax = glm::max(bbMax, p);
    };

    // Combine all meshes from scene
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];
        if (!m || !m->HasPositions()) continue;

        uint32_t baseVertex = (uint32_t)vertices.size();

        // Vertices
        for (unsigned int vi = 0; vi < m->mNumVertices; ++vi) {
            aiVector3D p = m->mVertices[vi];
            aiVector3D n = m->HasNormals() ? m->mNormals[vi] : aiVector3D(0,0,1);

            glm::vec3 gp(p.x, p.y, p.z);
            glm::vec3 gn(n.x, n.y, n.z);

            vertices.push_back({gp, gn});
            expandBB(gp);
        }

        // Faces (triangles after aiProcess_Triangulate)
        for (unsigned int fi = 0; fi < m->mNumFaces; ++fi) {
            const aiFace& f = m->mFaces[fi];
            if (f.mNumIndices != 3) continue;
            indices.push_back(baseVertex + (uint32_t)f.mIndices[0]);
            indices.push_back(baseVertex + (uint32_t)f.mIndices[1]);
            indices.push_back(baseVertex + (uint32_t)f.mIndices[2]);
        }
    }

    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("No triangles found in model: " + path);
    }

    // Upload to GPU
    GpuMesh gpu;
    gpu.bbMin = bbMin;
    gpu.bbMax = bbMax;
    gpu.indexCount = (GLsizei)indices.size();

    glGenVertexArrays(1, &gpu.vao);
    glGenBuffers(1, &gpu.vbo);
    glGenBuffers(1, &gpu.ebo);

    glBindVertexArray(gpu.vao);

    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vertices.size() * sizeof(Vertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(indices.size() * sizeof(uint32_t)),
                 indices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1); // nrm
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, nrm));

    glBindVertexArray(0);

    std::cout << "Loaded: " << path << "\n";
    std::cout << "Vertices: " << vertices.size() << "  Indices: " << indices.size()
              << "  Triangles: " << (indices.size()/3) << "\n";

    return gpu;
}

static void FitCameraToBounds(const glm::vec3& bmin, const glm::vec3& bmax) {
    glm::vec3 center = 0.5f * (bmin + bmax);
    glm::vec3 ext = 0.5f * (bmax - bmin);
    float radius = glm::length(ext);
    if (radius < 1e-6f) radius = 1.0f;

    gCam.target = center;
    gCam.distance = radius * 2.5f;
    gCam.yaw = glm::radians(45.0f);
    gCam.pitch = glm::radians(20.0f);
}

static void GlfwErrorCallback(int code, const char* msg) {
    std::cerr << "GLFW error " << code << ": " << msg << "\n";
}

int main(int argc, char** argv) {
    std::string diamondPath = (argc >= 2) ? argv[1] : kDefaultDiamondPath;
    std::string colletPath  = (argc >= 3) ? argv[2] : kDefaultColletPath;

    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(1280, 720, "Mesh Viewer (Phase 4 - Assimp)", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    glfwSetKeyCallback(win, KeyCallback);
    glfwSetMouseButtonCallback(win, MouseButtonCallback);
    glfwSetCursorPosCallback(win, CursorPosCallback);
    glfwSetScrollCallback(win, ScrollCallback);

    // ---- GLAD init ----
    // If using glad.h (GLAD1 style), use gladLoadGLLoader with GLFW proc address. [5](https://www.scivision.dev/install-msys2-windows/)[1](https://sibras.github.io/OpenGL4-Tutorials/docs/Tutorials/05-Tutorial5/)
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Determine initial framebuffer size
    int w=1280, h=720;
    glfwGetFramebufferSize(win, &w, &h);

    PathTracer gPT;
    if (!gPT.init(w, h)) {
        std::cerr << "PathTracer init failed\n";
        return 1;
    }

    // Load path tracer scene **from the same model path**:
    if (!gPT.loadSceneFromFiles(diamondPath, colletPath)) {
        std::cerr << "PathTracer scene load failed\n";
        // not fatal for raster mode; continue
    }
    gPT.setSamplesPerDispatch(4); // real-time default

    static GLint g_uBaseColorLoc = -1;


    GLuint prog = 0;
    try {
        prog = CompileShaderProgramFromFiles("shaders/mesh.vert", "shaders/mesh.frag");
        g_uBaseColorLoc = glGetUniformLocation(prog, "uBaseColor");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    // Load meshes (diamond + optional gold collet)
    GpuMesh meshDiamond;
    GpuMesh meshCollet;
    bool hasCollet = false;
    bool showCollet = true;

    try {
        meshDiamond = LoadMeshToGpu(diamondPath);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    try {
        meshCollet = LoadMeshToGpu(colletPath);
        hasCollet = true;
    } catch (const std::exception& e) {
        std::cerr << "[Warn] Collet mesh load failed: " << e.what() << "\n";
        hasCollet = false;
        showCollet = false;
    }

    // Fit camera to combined bounds so toggling doesn't jump the framing.
    glm::vec3 bbMin = meshDiamond.bbMin;
    glm::vec3 bbMax = meshDiamond.bbMax;
    if (hasCollet) {
        bbMin = glm::min(bbMin, meshCollet.bbMin);
        bbMax = glm::max(bbMax, meshCollet.bbMax);
    }
    FitCameraToBounds(bbMin, bbMax);
    gPT.setShowCollet(showCollet);


    GLint uMVP   = glGetUniformLocation(prog, "uMVP");
    GLint uModel = glGetUniformLocation(prog, "uModel");

    glm::mat4 M(1.0f);
    

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
    
        // Resize handling
        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
    
        // Keyboard toggles (debounced)
        bool P = glfwGetKey(win, GLFW_KEY_P) == GLFW_PRESS;
        bool V = glfwGetKey(win, GLFW_KEY_V) == GLFW_PRESS;
        bool R = glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS;
        bool C = glfwGetKey(win, GLFW_KEY_C) == GLFW_PRESS;
        bool SPACE = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (P && !gPrevP) { gUsePathTracer = true; gPT.resetAccum(); }
        if (V && !gPrevV) { gUsePathTracer = false; }
        if (R && !gPrevR) { if (gUsePathTracer) gPT.resetAccum(); }
        if (C && !gPrevC) {
            if (hasCollet) {
                showCollet = !showCollet;
                gPT.setShowCollet(showCollet);
            }
        }
        
        if (SPACE && !gPrevSpace) {
            // Optional: ensure center/distance are fresh against the current mesh bounds
            FitCameraToBounds(bbMin, bbMax);     // already present in your code path
            SetTopView();                                   // set pitch/yaw for top view
            if (gUsePathTracer) gPT.resetAccum();           // keep PT consistent with camera change
        }

        gPrevP = P; gPrevV = V; gPrevR = R; gPrevC = C; gPrevSpace = SPACE;
    
        // Derive camera matrices
        glm::mat4 Vmat = gCam.view();
        float aspect = (h > 0) ? float(w)/float(h) : 1.0f;
        glm::mat4 Pmat = glm::perspective(gCam.fovY, aspect, 0.001f, 1e7f);
        glm::mat4 MVP = Pmat * Vmat * M;
    
        // Compute camera basis for PT (re-derive eye/dir/right/up)
        float cp = std::cos(gCam.pitch);
        glm::vec3 forward(cp * std::cos(gCam.yaw),
                          std::sin(gCam.pitch),
                          cp * std::sin(gCam.yaw));
        glm::vec3 eye = gCam.target - forward * gCam.distance;
        glm::vec3 dir = glm::normalize(gCam.target - eye);
        glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0,1,0)));
        glm::vec3 up    = glm::normalize(glm::cross(right, dir));
    
        // If path tracer is active
        if (gUsePathTracer) {
            gPT.resize(w, h);
            gPT.setCamera(eye, dir, right, up, gCam.fovY, aspect);
    
            
            if (gCameraDirty) {
                gPT.resetAccum();
                gCameraDirty = false;
            }

            gPT.resize(w, h);  // resize() already resets when size changes
            gPT.setCamera(eye, dir, right, up, gCam.fovY, aspect);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            gPT.render();
            glfwSwapBuffers(win);
            continue;

        }
    
        // -------- Raster path (your existing viewer) --------
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
        glUseProgram(prog);
        glUniformMatrix4fv(uMVP,   1, GL_FALSE, glm::value_ptr(MVP));
        glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(M));

        // Diamond (neutral preview)
        if (g_uBaseColorLoc >= 0) glUniform3f(g_uBaseColorLoc, 0.85f, 0.85f, 0.90f);
        glBindVertexArray(meshDiamond.vao);
        glDrawElements(GL_TRIANGLES, meshDiamond.indexCount, GL_UNSIGNED_INT, (void*)0);
        glBindVertexArray(0);

        // Gold collet (simple shaded preview; path tracer is the real evaluation)
        if (hasCollet && showCollet) {
            if (g_uBaseColorLoc >= 0) glUniform3f(g_uBaseColorLoc, 1.00f, 0.78f, 0.34f);
            glBindVertexArray(meshCollet.vao);
            glDrawElements(GL_TRIANGLES, meshCollet.indexCount, GL_UNSIGNED_INT, (void*)0);
            glBindVertexArray(0);
        }


glfwSwapBuffers(win);
    }

    glDeleteProgram(prog);

    glDeleteBuffers(1, &meshDiamond.ebo);
    glDeleteBuffers(1, &meshDiamond.vbo);
    glDeleteVertexArrays(1, &meshDiamond.vao);

    if (hasCollet) {
        glDeleteBuffers(1, &meshCollet.ebo);
        glDeleteBuffers(1, &meshCollet.vbo);
        glDeleteVertexArrays(1, &meshCollet.vao);
    }
glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
