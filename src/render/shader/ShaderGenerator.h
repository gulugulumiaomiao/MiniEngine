#pragma once

#include "render/shader/Shader.h"

#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

class ShaderGenerator final {
public:
    [[nodiscard]] std::optional<std::string> generateStage(const Shader& shader,
                                                           const ShaderPass& pass,
                                                           ShaderStage stage,
                                                           std::string_view source) const;

private:
    [[nodiscard]] static bool validIdentifier(const std::string& value);
    [[nodiscard]] static const char* glslType(ShaderPropertyType type);
    [[nodiscard]] static const char* glslType(ShaderValueType type);
    [[nodiscard]] static const char* interpolationQualifier(ShaderInterpolation interpolation);
    [[nodiscard]] static bool
    validateInterface(const std::vector<ShaderInterfaceVariable>& variables,
                      const char* interfaceName);
    [[nodiscard]] static bool writeStruct(std::ostringstream& output,
                                          const char* name,
                                          const std::vector<ShaderInterfaceVariable>& variables);
    [[nodiscard]] static bool
    writeLocationDeclarations(std::ostringstream& output,
                              const char* direction,
                              const char* prefix,
                              const std::vector<ShaderInterfaceVariable>& variables,
                              bool includeInterpolation);
    [[nodiscard]] static std::string lineDirectiveName(const std::string& value);
    static void writeUserSource(std::ostringstream& output,
                                const ShaderPassDesc& pass,
                                const VirtualPath& sourcePath,
                                std::string_view stageName,
                                std::string_view userSource);
    [[nodiscard]] static std::optional<std::string>
    generateVertexStage(const ShaderPassDesc& pass,
                        std::string_view materialDeclarations,
                        std::string_view userSource);
    [[nodiscard]] static std::optional<std::string>
    generateFragmentStage(const ShaderPassDesc& pass,
                          std::string_view materialDeclarations,
                          std::string_view userSource);
    [[nodiscard]] static bool validateLayout(std::span<const ShaderPropertyDesc> properties,
                                             const UniformBlockLayout& layout);
    [[nodiscard]] static std::optional<std::string>
    generateMaterialDeclarations(std::span<const ShaderPropertyDesc> properties,
                                 const UniformBlockLayout& layout);
};

} // namespace engine
