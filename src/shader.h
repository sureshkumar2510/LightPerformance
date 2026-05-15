#pragma once
#include <string>
#include <cstdint>

uint32_t CompileShaderProgramFromFiles(const std::string& vsPath, const std::string& fsPath);

// NEW: compile a standalone compute program from a file
uint32_t CompileComputeProgramFromFile(const std::string& csPath);