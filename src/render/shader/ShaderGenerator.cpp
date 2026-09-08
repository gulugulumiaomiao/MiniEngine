#include "render/shader/ShaderGenerator.h"

#include "core/logging/Log.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace engine {

bool ShaderGenerator::validIdentifier(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(value.front());
    if (std::isalpha(first) == 0 && value.front() != '_') {
        return false;
    }
    return std::ranges::all_of(value, [](char character) {
        const auto value = static_cast<unsigned char>(character);
        return std::isalnum(value) != 0 || character == '_';
    });
}

const char* ShaderGenerator::glslType(ShaderPropertyType type) {
    switch (type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Range: return "float";
    case ShaderPropertyType::Boolean: return "uint";
    case ShaderPropertyType::Vec2: return "vec2";
    case ShaderPropertyType::Vec3: return "vec3";
    case ShaderPropertyType::Vec4:
    case ShaderPropertyType::Color: return "vec4";
    case ShaderPropertyType::Texture2D: return "sampler2D";
    }
    Log::error("ShaderGenerator", "Unsupported Shader property type");
    return nullptr;
}

const char* ShaderGenerator::glslType(ShaderValueType type) {
    switch (type) {
    case ShaderValueType::Float: return "float";
    case ShaderValueType::Vec2: return "vec2";
    case ShaderValueType::Vec3: return "vec3";
    case ShaderValueType::Vec4: return "vec4";
    }
    Log::error("ShaderGenerator", "Unsupported Shader interface type");
    return nullptr;
}

const char* ShaderGenerator::interpolationQualifier(ShaderInterpolation interpolation) {
    switch (interpolation) {
    case ShaderInterpolation::Smooth: return "smooth";
    case ShaderInterpolation::Flat: return "flat";
    case ShaderInterpolation::NoPerspective: return "noperspective";
    }
    Log::error("ShaderGenerator", "Unsupported Shader interpolation mode");
    return nullptr;
}

bool ShaderGenerator::validateInterface(const std::vector<ShaderInterfaceVariable>& variables,
                                        const char* interfaceName) {
    for (const ShaderInterfaceVariable& variable : variables) {
        if (!validIdentifier(variable.name)) {
            Log::error("ShaderGenerator",
                       "%s contains an invalid GLSL identifier: %s",
                       interfaceName,
                       variable.name.c_str());
            return false;
        }
    }
    return true;
}

bool ShaderGenerator::writeStruct(std::ostringstream& output,
                                  const char* name,
                                  const std::vector<ShaderInterfaceVariable>& variables) {
    output << "struct " << name << "\n{\n";
    if (variables.empty()) {
        output << "    uint _unused;\n";
    } else {
        for (const ShaderInterfaceVariable& variable : variables) {
            const char* type = glslType(variable.type);
            if (!type)
                return false;
            output << "    " << type << ' ' << variable.name << ";\n";
        }
    }
    output << "};\n\n";
    return true;
}

bool ShaderGenerator::writeLocationDeclarations(
    std::ostringstream& output,
    const char* direction,
    const char* prefix,
    const std::vector<ShaderInterfaceVariable>& variables,
    bool includeInterpolation) {
    for (const ShaderInterfaceVariable& variable : variables) {
        output << "layout(location = " << variable.location << ") ";
        if (includeInterpolation) {
            const char* qualifier = interpolationQualifier(variable.interpolation);
            if (!qualifier)
                return false;
            output << qualifier << ' ';
        }
        const char* type = glslType(variable.type);
        if (!type)
            return false;
        output << direction << ' ' << type << ' ' << prefix << variable.name << ";\n";
    }
    if (!variables.empty()) {
        output << '\n';
    }
    return true;
}

std::string ShaderGenerator::lineDirectiveName(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

void ShaderGenerator::writeUserSource(std::ostringstream& output,
                                      const ShaderPassDesc& pass,
                                      const VirtualPath& sourcePath,
                                      std::string_view stageName,
                                      std::string_view userSource) {
    const std::string sourceName = lineDirectiveName(sourcePath.string());
    const std::string wrapperName = lineDirectiveName("MiniShaderCompiler/" + pass.name + "/" +
                                                      std::string{stageName} + "-wrapper");
    output << "// User shader source.\n#line 1 \"" << sourceName << "\"\n" << userSource;
    if (userSource.empty() || userSource.back() != '\n') {
        output << '\n';
    }
    output << "#line 1 \"" << wrapperName << "\"\n\n";
}

std::shared_ptr<std::string>
ShaderGenerator::generateVertexStage(const ShaderPassDesc& pass,
                                     std::string_view materialDeclarations,
                                     std::string_view userSource) {
    std::ostringstream output;
    output << "#version 450\n"
              "#extension GL_GOOGLE_cpp_style_line_directive : enable\n"
              "#define MINI_VERTEX_STAGE 1\n\n";
    output << materialDeclarations << '\n';
    if (!writeStruct(output, "MiniVertexInput", pass.vertexInput) ||
        !writeStruct(output, "MiniVaryings", pass.varyings) ||
        !writeLocationDeclarations(output, "in", "_MiniIn_", pass.vertexInput, false) ||
        !writeLocationDeclarations(output, "out", "_MiniOut_", pass.varyings, true)) {
        return {};
    }
    writeUserSource(output, pass, pass.program.vertexSource, "vertex", userSource);
    output << "// Generated vertex entry wrapper.\nvoid main()\n{\n"
              "    MiniVertexInput inputValue;\n";
    for (const ShaderInterfaceVariable& variable : pass.vertexInput) {
        output << "    inputValue." << variable.name << " = _MiniIn_" << variable.name << ";\n";
    }
    output << "    MiniVaryings outputValue;\n"
           << "    VertexMain(inputValue, outputValue);\n";
    for (const ShaderInterfaceVariable& variable : pass.varyings) {
        output << "    _MiniOut_" << variable.name << " = outputValue." << variable.name << ";\n";
    }
    output << "}\n";
    return std::make_shared<std::string>(std::move(output).str());
}

std::shared_ptr<std::string>
ShaderGenerator::generateFragmentStage(const ShaderPassDesc& pass,
                                       std::string_view materialDeclarations,
                                       std::string_view userSource) {
    std::ostringstream output;
    output << "#version 450\n"
              "#extension GL_GOOGLE_cpp_style_line_directive : enable\n"
              "#define MINI_FRAGMENT_STAGE 1\n\n";
    output << materialDeclarations << '\n';
    if (!writeStruct(output, "MiniVaryings", pass.varyings) ||
        !writeStruct(output, "MiniFragmentOutput", pass.fragmentOutputs) ||
        !writeLocationDeclarations(output, "in", "_MiniIn_", pass.varyings, true) ||
        !writeLocationDeclarations(output, "out", "_MiniOut_", pass.fragmentOutputs, false)) {
        return {};
    }
    writeUserSource(output, pass, pass.program.fragmentSource, "fragment", userSource);
    output << "// Generated fragment entry wrapper.\nvoid main()\n{\n"
              "    MiniVaryings inputValue;\n";
    for (const ShaderInterfaceVariable& variable : pass.varyings) {
        output << "    inputValue." << variable.name << " = _MiniIn_" << variable.name << ";\n";
    }
    output << "    MiniFragmentOutput outputValue;\n"
           << "    FragmentMain(inputValue, outputValue);\n";
    for (const ShaderInterfaceVariable& variable : pass.fragmentOutputs) {
        output << "    _MiniOut_" << variable.name << " = outputValue." << variable.name << ";\n";
    }
    output << "}\n";
    return std::make_shared<std::string>(std::move(output).str());
}

bool ShaderGenerator::validateLayout(std::span<const ShaderPropertyDesc> properties,
                                     const UniformBlockLayout& layout) {
    std::size_t uniformIndex = 0;
    for (const ShaderPropertyDesc& property : properties) {
        if (!validIdentifier(property.name)) {
            Log::error("ShaderGenerator",
                       "Shader property is not a valid GLSL identifier: %s",
                       property.name.c_str());
            return false;
        }
        if (!isUniformProperty(property.type)) {
            continue;
        }
        if (uniformIndex >= layout.members.size()) {
            Log::error("ShaderGenerator",
                       "Material uniform layout is missing property: %s",
                       property.name.c_str());
            return false;
        }
        const UniformMemberLayout& member = layout.members[uniformIndex++];
        if (member.name != property.name || member.type != property.type) {
            Log::error("ShaderGenerator",
                       "Material uniform layout does not match Shader property: %s",
                       property.name.c_str());
            return false;
        }
    }
    if (uniformIndex != layout.members.size()) {
        Log::error("ShaderGenerator", "Material uniform layout contains undeclared members");
        return false;
    }
    return true;
}

std::shared_ptr<std::string>
ShaderGenerator::generateMaterialDeclarations(std::span<const ShaderPropertyDesc> properties,
                                              const UniformBlockLayout& layout) {
    if (!validateLayout(properties, layout))
        return {};

    std::ostringstream output;
    output << "// Generated from ShaderLab properties. Do not edit.\n";
    if (!layout.members.empty()) {
        output << "layout(std140, set = 1, binding = 0) uniform MaterialProperties\n{\n";
        for (const UniformMemberLayout& member : layout.members) {
            const char* type = glslType(member.type);
            if (!type)
                return {};
            output << "    layout(offset = " << member.offset << ") " << type << ' ' << member.name
                   << ";\n";
        }
        output << "} Material;\n";
    }

    std::uint32_t textureBinding = 1;
    for (const ShaderPropertyDesc& property : properties) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        if (textureBinding != 1 || !layout.members.empty())
            output << '\n';
        output << "layout(set = 1, binding = " << textureBinding << ") uniform sampler2D "
               << property.name << ";\n";
        if (textureBinding == std::numeric_limits<std::uint32_t>::max()) {
            Log::error("ShaderGenerator", "Generated texture binding overflow");
            return {};
        }
        ++textureBinding;
    }
    return std::make_shared<std::string>(std::move(output).str());
}

std::shared_ptr<std::string> ShaderGenerator::generateStage(const Shader& shader,
                                                            const ShaderPass& pass,
                                                            ShaderStage stage,
                                                            std::string_view source) const {
    ShaderPassDesc desc;
    desc.name = pass.name();
    desc.type = pass.type();
    desc.program = pass.program();
    desc.vertexInput = pass.vertexInput();
    desc.varyings = pass.varyings();
    desc.fragmentOutputs = pass.fragmentOutputs();
    desc.features = pass.features();
    const auto declarations =
        generateMaterialDeclarations(shader.properties(), shader.uniformBlockLayout());
    if (!declarations || !desc.program.hasSourceProgram() || source.empty() ||
        !validateInterface(desc.vertexInput, "vertexInput") ||
        !validateInterface(desc.varyings, "varyings") ||
        !validateInterface(desc.fragmentOutputs, "fragmentOutputs")) {
        return {};
    }
    return stage == ShaderStage::Vertex ? generateVertexStage(desc, *declarations, source)
                                        : generateFragmentStage(desc, *declarations, source);
}

} // namespace engine
