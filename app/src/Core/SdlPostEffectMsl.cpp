#include "Core/SdlPostEffectMsl.hpp"

#include <exception>
#include <algorithm>

#include "spirv_cross.hpp"
#include "spirv_msl.hpp"

bool compileSdlPostEffectMsl(std::span<const std::uint32_t> spirv,
                             std::string& source,
                             std::string& entryPoint,
                             std::string& error)
{
    source.clear();
    entryPoint.clear();
    error.clear();
    if (spirv.empty()) {
        error = "SdlPostEffectMsl: SPIR-V module is empty";
        return false;
    }

    try {
        spirv_cross::CompilerMSL compiler(spirv.data(), spirv.size());
        const auto entries = compiler.get_entry_points_and_stages();
        const auto entry = std::find_if(entries.begin(), entries.end(), [](const auto& item) {
            return item.execution_model == spv::ExecutionModelFragment;
        });
        if (entry == entries.end()) {
            error = "SdlPostEffectMsl: SPIR-V module has no fragment entry point";
            return false;
        }
        compiler.set_entry_point(entry->name, entry->execution_model);

        // SDL_GPU's fragment-stage convention uses descriptor set 2 for the
        // texture/sampler pair and set 3 for uniform buffers. MSL has no
        // descriptor sets, so make the resource indices explicit and stable.
        const auto resources = compiler.get_shader_resources();
        if (!resources.sampled_images.empty() &&
            (!resources.separate_images.empty() || !resources.separate_samplers.empty())) {
            error = "SdlPostEffectMsl: mixed combined and separate image resources are unsupported";
            return false;
        }
        std::uint32_t textureBinding = 0;
        for (const auto& resource : resources.sampled_images) {
            compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 2);
            compiler.set_decoration(resource.id, spv::DecorationBinding, textureBinding++);
        }
        textureBinding = 0;
        for (const auto& resource : resources.separate_images) {
            compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 2);
            compiler.set_decoration(resource.id, spv::DecorationBinding, textureBinding++);
        }
        std::uint32_t samplerBinding = 0;
        for (const auto& resource : resources.separate_samplers) {
            compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 2);
            compiler.set_decoration(resource.id, spv::DecorationBinding, samplerBinding++);
        }
        std::uint32_t uniformBinding = 0;
        for (const auto& resource : resources.uniform_buffers) {
            compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 3);
            compiler.set_decoration(resource.id, spv::DecorationBinding, uniformBinding++);
        }

        auto options = compiler.get_msl_options();
        options.enable_decoration_binding = true;
        options.argument_buffers = false;
        compiler.set_msl_options(options);
        source = compiler.compile();
        if (source.empty()) {
            error = "SdlPostEffectMsl: SPIRV-Cross produced empty MSL";
            entryPoint.clear();
            return false;
        }
        entryPoint = compiler.get_cleansed_entry_point_name(entry->name, entry->execution_model);
        if (entryPoint.empty()) {
            error = "SdlPostEffectMsl: SPIRV-Cross returned no MSL entry point";
            source.clear();
            return false;
        }
        return true;
    } catch (const spirv_cross::CompilerError& exception) {
        error = "SdlPostEffectMsl: SPIRV-Cross failed: ";
        error += exception.what();
    } catch (const std::exception& exception) {
        error = "SdlPostEffectMsl: shader conversion failed: ";
        error += exception.what();
    }
    return false;
}
