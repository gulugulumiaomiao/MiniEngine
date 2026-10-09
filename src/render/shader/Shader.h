#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "render/shader/ShaderTypes.h"
#include "render/shader/ShaderProgram.h"

#include <unordered_map>

namespace engine {

class ShaderPass final {
public:
    explicit ShaderPass(const ShaderPassCreateInfo& desc);

    [[nodiscard]] ShaderPassType type() const { return type_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const std::vector<ShaderInterfaceVariable>& vertexInput() const {
        return vertexInput_;
    }
    [[nodiscard]] const std::vector<ShaderInterfaceVariable>& varyings() const { return varyings_; }
    [[nodiscard]] const std::vector<ShaderInterfaceVariable>& fragmentOutputs() const {
        return fragmentOutputs_;
    }
    [[nodiscard]] const RenderState& renderState() const { return renderState_; }
    [[nodiscard]] const ShaderKeywordSchema& keyword() const { return keyword_; }
    [[nodiscard]] Ref<ShaderProgram> findProgram(std::uint64_t key) const;
    [[nodiscard]] std::size_t programCount() const { return programs_.size(); }

private:
    std::string name_;
    ShaderPassType type_{ShaderPassType::Forward};
    std::vector<ShaderInterfaceVariable> vertexInput_;
    std::vector<ShaderInterfaceVariable> varyings_;
    std::vector<ShaderInterfaceVariable> fragmentOutputs_;
    RenderState renderState_;
    ShaderKeywordSchema keyword_;
    mutable std::unordered_map<std::uint64_t, Ref<ShaderProgram>> programs_;
    friend class Shader;
    friend class GraphicsPipelineStorage;
};

class SubShader final {
public:
    explicit SubShader(const SubShaderCreateInfo& desc);

    [[nodiscard]] const std::string& renderPipeline() const { return renderPipeline_; }
    [[nodiscard]] int renderQueue() const { return renderQueue_; }
    [[nodiscard]] const std::vector<ShaderPass>& passes() const { return passes_; }
    [[nodiscard]] const ShaderPass* findPass(ShaderPassType type) const;
    [[nodiscard]] const ShaderPass& requirePass(ShaderPassType type) const;
    [[nodiscard]] bool supports(std::string_view renderPipeline) const;

private:
    std::string renderPipeline_;
    int renderQueue_{2000};
    std::vector<ShaderPass> passes_;
};

class Shader final : public RefCounted {
public:
    explicit Shader(ShaderCreateInfo info);
    ~Shader() override;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&&) = delete;
    Shader& operator=(Shader&&) = delete;

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const std::vector<ShaderPropertyInfo>& properties() const { return properties_; }
    [[nodiscard]] const std::vector<ShaderPropertyInfo>& globalProperties() const {
        return globalProperties_;
    }
    [[nodiscard]] const UniformBlockLayout& uniformBlockLayout() const {
        return uniformBlockLayout_;
    }
    [[nodiscard]] const UniformBlockLayout& globalUniformBlockLayout() const {
        return globalUniformBlockLayout_;
    }
    [[nodiscard]] const std::vector<SubShader>& subShaders() const { return subShaders_; }
    [[nodiscard]] const SubShader* selectSubShader(std::string_view renderPipeline) const;
    [[nodiscard]] const SubShader& requireSubShader(std::string_view renderPipeline) const;
    [[nodiscard]] const SubShader& defaultSubShader() const;
    [[nodiscard]] bool declaresKeyword(std::string_view keyword) const;
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    // Compilation input belongs to Shader, never to ShaderPass.
    [[nodiscard]] const ShaderProgramDesc& programSource(const ShaderPass& pass) const;

    [[nodiscard]] Ref<Shader> clone() const;
    [[nodiscard]] bool rebuild(ShaderCreateInfo info);
    [[nodiscard]] ShaderCompileKey compileKey() const { return compileKey_; }
    [[nodiscard]] std::uint64_t instanceId() const { return instanceId_; }

    // Unity-style global shader property API. Values set here are shared across
    // all shaders that declare a matching global property.
    static void setGlobalFloat(std::string_view name, float value);
    static void setGlobalInt(std::string_view name, int value);
    static void setGlobalVector(std::string_view name, const math::Vec4& value);
    static void setGlobalColor(std::string_view name, const math::Vec4& value);
    static void setGlobalBool(std::string_view name, bool value);
    static void setGlobalMatrix(std::string_view name, const math::Mat44& value);
    static void setGlobalTexture(std::string_view name, std::string_view texturePath);

private:
    [[nodiscard]] ShaderCreateInfo snapshot() const;
    static void clearProgramCaches();
    friend class GraphicsPipelineStorage;

    std::string name_;
    std::vector<ShaderPropertyInfo> properties_;
    std::vector<ShaderPropertyInfo> globalProperties_;
    UniformBlockLayout uniformBlockLayout_;
    UniformBlockLayout globalUniformBlockLayout_;
    std::vector<SubShader> subShaders_;
    std::vector<std::vector<ShaderProgramDesc>> programSources_;
    std::uint64_t revision_{1};

    ShaderCompileKey compileKey_{};
    std::uint64_t instanceId_{};
};

} // namespace engine
