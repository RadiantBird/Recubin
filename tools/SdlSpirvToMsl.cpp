#include "spirv_cross.hpp"
#include "spirv_msl.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
bool supportedSet(std::uint32_t set, const std::string& stage, const char* kind) {
    if (stage == "vertex") {
        if (std::string(kind) == "uniform buffer") return set == 1;
        return set == 0;
    }
    if (std::string(kind) == "uniform buffer") return set == 3;
    return set == 2;
}

bool convert(const std::vector<std::uint32_t>& words, const std::string& stage,
             const std::string& output, std::string& error) {
    try {
        if (stage != "vertex" && stage != "fragment") {
            error = "invalid shader stage: " + stage;
            return false;
        }
        spirv_cross::CompilerMSL compiler(words);
        const auto model = stage == "vertex" ? spv::ExecutionModelVertex
                                              : spv::ExecutionModelFragment;
        const auto entries = compiler.get_entry_points_and_stages();
        const auto entry = std::find_if(entries.begin(), entries.end(),
                                        [model](const auto& item) {
                                            return item.execution_model == model;
                                        });
        if (entry == entries.end()) {
            error = "SPIR-V has no requested " + stage + " entry point";
            return false;
        }
        compiler.set_entry_point(entry->name, model);
        const auto resources = compiler.get_shader_resources();
        const auto validateResources = [&compiler, &stage, &error](const auto& list,
                                                            std::uint32_t expectedSet,
                                                            const char* kind) {
            for (const auto& resource : list) {
                if (!compiler.has_decoration(resource.id, spv::DecorationDescriptorSet) ||
                    !compiler.has_decoration(resource.id, spv::DecorationBinding)) {
                    error = std::string("missing SDL binding for ") + kind + " '" +
                            compiler.get_name(resource.id) + "' in " + stage + " stage";
                    return false;
                }
                const auto set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                const auto binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                if (set != expectedSet) {
                    error = std::string("invalid SDL descriptor set for ") + kind + " '" +
                            compiler.get_name(resource.id) + "': set " + std::to_string(set) +
                            ", expected " + std::to_string(expectedSet);
                    return false;
                }
                (void)binding;
            }
            return true;
        };
        const std::uint32_t resourceSet = stage == "vertex" ? 0u : 2u;
        const std::uint32_t uniformSet = stage == "vertex" ? 1u : 3u;
        if (!validateResources(resources.uniform_buffers, uniformSet, "uniform buffer") ||
            !validateResources(resources.sampled_images, resourceSet, "sampled image") ||
            !validateResources(resources.separate_images, resourceSet, "separate image") ||
            !validateResources(resources.separate_samplers, resourceSet, "separate sampler") ||
            !validateResources(resources.storage_images, resourceSet, "storage image")) {
            return false;
        }
        auto options = compiler.get_msl_options();
        options.enable_decoration_binding = true;
        options.argument_buffers = false;
        compiler.set_msl_options(options);
        const auto source = compiler.compile();
        if (source.empty()) {
            error = "SPIRV-Cross produced empty MSL";
            return false;
        }
        const auto outputEntry = compiler.get_cleansed_entry_point_name(entry->name, model);
        if (outputEntry.empty()) {
            error = "SPIRV-Cross produced no MSL entry point";
            return false;
        }
        std::ofstream stream(output, std::ios::binary);
        if (!stream) {
            error = "cannot open MSL output: " + output;
            return false;
        }
        stream << source;
        return static_cast<bool>(stream);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: SdlSpirvToMsl <vertex|fragment> <input.spv> <output.msl>\n";
        return 2;
    }
    std::ifstream stream(argv[2], std::ios::binary);
    if (!stream) {
        std::cerr << "cannot open SPIR-V input: " << argv[2] << '\n';
        return 1;
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), {});
    if (bytes.size() % sizeof(std::uint32_t) != 0) {
        std::cerr << "SPIR-V input size is not word aligned\n";
        return 1;
    }
    std::vector<std::uint32_t> aligned(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(aligned.data(), bytes.data(), bytes.size());
    std::string error;
    if (!convert(aligned, argv[1], argv[3], error)) {
        std::cerr << error << '\n';
        return 1;
    }
    return 0;
}
