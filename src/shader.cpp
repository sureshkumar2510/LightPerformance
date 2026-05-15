#include "shader.h"
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <glad/glad.h>

static std::string ReadTextFile(const std::string& path) {
    std::ifstream f(path, std::ios::in);
    if (!f) throw std::runtime_error("Failed to open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static uint32_t Compile(uint32_t type, const std::string& src) {
    uint32_t s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        int len = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::string msg(log.begin(), log.end());
        throw std::runtime_error("Shader compile failed: " + msg);
    }
    return s;
}

uint32_t CompileShaderProgramFromFiles(const std::string& vsPath, const std::string& fsPath) {
    auto vs = ReadTextFile(vsPath);
    auto fs = ReadTextFile(fsPath);

    uint32_t v = Compile(GL_VERTEX_SHADER, vs);
    uint32_t f = Compile(GL_FRAGMENT_SHADER, fs);

    uint32_t p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);

    glDeleteShader(v);
    glDeleteShader(f);

    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        int len = 0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::string msg(log.begin(), log.end());
        throw std::runtime_error("Program link failed: " + msg);
    }
    return p;
}

// NEW
uint32_t CompileComputeProgramFromFile(const std::string& csPath) {
    auto cs = ReadTextFile(csPath);
    uint32_t c = Compile(GL_COMPUTE_SHADER, cs);

    uint32_t p = glCreateProgram();
    glAttachShader(p, c);
    glLinkProgram(p);
    glDeleteShader(c);

    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        int len = 0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::string msg(log.begin(), log.end());
        throw std::runtime_error("Compute program link failed: " + msg);
    }
    return p;
}