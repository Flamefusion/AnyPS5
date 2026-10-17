#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libSceAgc/Shader/include/ShaderConstants.hpp"
#include "VulkanTestDevice.hpp"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using AgcDriver::DriverDetail::DrawMiss;

alignas(256) constexpr std::array<std::uint32_t, 8> VertexCode{0x7e000208u, 0x7e020280u, 0x7e040280u, 0x7e0602f2u, 0xf80008cfu, 0x03020100u, 0xbf810000u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 8> SecondVertexCode = VertexCode;
alignas(256) constexpr std::array<std::uint32_t, 8> FetchVertexCode = VertexCode;
alignas(256) constexpr std::array<std::uint32_t, 8> PixelCode{0xf4000100u, 0xfa000000u, 0x7e000204u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u, 0xbf810000u};
alignas(16) std::array<float, 4> Other{0.5f, 0.0f, 0.0f, 0.0f};
alignas(256) std::array<std::byte, 1024> ColorMemory{};

struct Header {
    Shader shader{};
    ShaderUserData userData{};
};

alignas(8) Header VertexHeader{};
alignas(8) Header PixelHeader{};
alignas(8) Header SecondVertexHeader{};

constexpr std::uint16_t FetchBufferWord = 2;
constexpr std::uint16_t FetchAttribWord = 4;
constexpr std::uint32_t FetchUserWords = 6;

struct FetchHeader {
    Shader shader{};
    ShaderUserData userData{};
    std::array<std::uint16_t, ShaderRegs::AGC_DIRECT_RESOURCE_TYPE_COUNT> direct{};
    ShaderSemantic semantic{};
};

alignas(8) FetchHeader FetchVertexHeader{};

struct FetchTables {
    alignas(16) std::array<std::uint32_t, 4> buffer{};
    std::array<std::uint32_t, 4> attrib{};
};

alignas(256) std::array<std::array<float, 4>, 4> FetchVertices{};

FetchTables Tables(std::uint32_t stride) {
    const auto address = reinterpret_cast<std::uintptr_t>(FetchVertices.data());
    FetchTables tables;
    tables.buffer = {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), 3u, 0x01000facu | (77u << 12u)};
    tables.attrib = {((77u << 2u) | 3u) << 5u, 0u, 0u, 0u};
    return tables;
}

template <typename THeader>
const Shader* Registered(THeader& header, const std::uint32_t* code, std::size_t bytes, std::uint8_t type) {
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(THeader);
    header.shader.shader_size = static_cast<std::uint32_t>(bytes);
    header.shader.code = code;
    header.shader.type = type;
    header.shader.user_data = &header.userData;
    AgcDriverRegisterShader_nid_postfix(&header.shader);
    return &header.shader;
}

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

std::vector<std::uint32_t> DrawFrom(std::initializer_list<const float*> sources, const std::uint32_t* vertexCode = VertexCode.data(), const FetchTables* tables = nullptr) {
    const auto color = reinterpret_cast<std::uintptr_t>(ColorMemory.data());
    const auto vertex = reinterpret_cast<std::uintptr_t>(vertexCode);
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, 0x28028},
        {0x31b, 0}, {0x31d, 0}, {0x3b0, (63u << 14u) | 3u},
        {0x3b8, 0x9000000}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x40040},
        {0x81, 0x80000000}, {0x82, 0x40040},
        {0x90, 0x80000000}, {0x91, 0x40040},
        {0x94, 0x80000000}, {0x95, 0x40040},
        {0x318, static_cast<std::uint32_t>(color >> 8u)}, {0x390, static_cast<std::uint32_t>(color >> 40u)},
        {0x10f, std::bit_cast<std::uint32_t>(32.0f)}, {0x110, std::bit_cast<std::uint32_t>(32.0f)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
    };
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> shader{
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, 1u << 1u}, {0x8c, 0},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 2u << 1u},
    };
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, 4});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    if (tables != nullptr) {
        const auto buffer = reinterpret_cast<std::uintptr_t>(tables->buffer.data());
        const auto attrib = reinterpret_cast<std::uintptr_t>(tables->attrib.data());
        Append(commands, 0x76, {0x8b, FetchUserWords << 1u});
        Append(commands, 0x76, {0x8c + FetchBufferWord, static_cast<std::uint32_t>(buffer)});
        Append(commands, 0x76, {0x8c + FetchBufferWord + 1u, static_cast<std::uint32_t>(buffer >> 32u)});
        Append(commands, 0x76, {0x8c + FetchAttribWord, static_cast<std::uint32_t>(attrib)});
        Append(commands, 0x76, {0x8c + FetchAttribWord + 1u, static_cast<std::uint32_t>(attrib >> 32u)});
    }
    for (const auto* source : sources) {
        const auto address = reinterpret_cast<std::uintptr_t>(source);
        Append(commands, 0x76, {0x00c, static_cast<std::uint32_t>(address)});
        Append(commands, 0x76, {0x00d, static_cast<std::uint32_t>(address >> 32u)});
        Append(commands, 0x2d, {3, 2});
    }
    return commands;
}

void MovedDataHitsTheCache(bool libraries) {
    Registered(VertexHeader, VertexCode.data(), sizeof(VertexCode), 2);
    Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
    const auto mode = std::string(libraries ? "with" : "without") + " graphics pipeline libraries";
    const auto draw = [&](std::initializer_list<const float*> sources) {
        auto commands = DrawFrom(sources);
        Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
        Require(sceAgcDriverSubmitDcb(&packet) == 0, "the draw submission failed");
        AgcDriverWaitIdle_nid_postfix();
        return AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    };
    static std::array<std::array<float, 4>, 8> same{};
    for (auto& entry : same) entry = {1.0f, 0.0f, 0.0f, 0.0f};
    AgcDriver::DriverDetail::DrawEntryCounters counters{};
    for (std::size_t i = 0; i < 4; ++i) counters = draw({same[i].data()});
    Require(counters.lookups == 4 && counters.hits == 0 && counters.inserts == 2, mode + ": the draws that teach the moved pointer and its contents made " + std::to_string(counters.hits) + " hits and " + std::to_string(counters.inserts) + " inserts, expected 0 and 2");
    for (std::size_t i = 4; i < same.size(); ++i) counters = draw({same[i].data()});
    Require(counters.lookups == 8 && counters.hits == 4 && counters.inserts == 2, mode + ": draws whose data moved again did not reuse the cached draw (" + std::to_string(counters.hits) + " hits in " + std::to_string(counters.lookups) + " lookups)");
    const auto other = draw({Other.data()});
    Require(other.lookups == 9 && other.hits == 4, mode + ": a draw whose moved data holds another value reused the cached draw");
    const auto again = draw({same[0].data()});
    Require(again.lookups == 10 && again.hits == 5, mode + ": a draw at an address seen before did not reuse the cached draw (" + std::to_string(again.hits) + " hits)");
}

void UnrelocatedAliasesStayOut(bool libraries) {
    Registered(SecondVertexHeader, SecondVertexCode.data(), sizeof(SecondVertexCode), 2);
    Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
    const auto mode = std::string(libraries ? "with" : "without") + " graphics pipeline libraries";
    const auto draw = [&](const float* source) {
        auto commands = DrawFrom({source}, SecondVertexCode.data());
        Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
        Require(sceAgcDriverSubmitDcb(&packet) == 0, "the draw submission failed");
        AgcDriverWaitIdle_nid_postfix();
        return AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    };
    alignas(16) static std::array<float, 4> repeated{1.0f, 0.0f, 0.0f, 0.0f};
    alignas(16) static std::array<std::array<std::array<float, 4>, 2>, 12> twice{};
    for (std::size_t i = 0; i < twice.size(); ++i) {
        for (auto& entry : twice[i]) entry = {0.25f + 0.03125f * static_cast<float>(i), 0.0f, 0.0f, 0.0f};
    }
    draw(repeated.data());
    const auto start = draw(repeated.data());
    AgcDriver::DriverDetail::DrawEntryCounters counters = start;
    for (const auto& pair : twice) {
        for (const auto& entry : pair) draw(entry.data());
        counters = draw(repeated.data());
    }
    const auto inserts = counters.inserts - start.inserts;
    Require(inserts <= AgcDriver::DriverDetail::DrawRelocationTrial::ProbationAdmissions, mode + ": draws whose alias repeats but never relocates were cached (" + std::to_string(inserts) + " inserts for " + std::to_string(twice.size()) + " aliases)");
    Require(counters.hits - start.hits == twice.size(), mode + ": the exact key of a repeated draw lost its hits next to draws whose alias never relocates (" + std::to_string(counters.hits - start.hits) + " hits in " + std::to_string(twice.size()) + " draws)");
}

void RegisterFetchVertex() {
    FetchVertexHeader.direct.fill(ShaderRegs::AGC_ILLEGAL_DIRECT_OFFSET);
    FetchVertexHeader.direct[static_cast<std::size_t>(ShaderRegs::AgcDirectResourceType::PtrVertexBufferTable)] = FetchBufferWord;
    FetchVertexHeader.direct[static_cast<std::size_t>(ShaderRegs::AgcDirectResourceType::PtrVertexAttribDescTable)] = FetchAttribWord;
    FetchVertexHeader.userData.direct_resource_offset = FetchVertexHeader.direct.data();
    FetchVertexHeader.userData.direct_resource_count = ShaderRegs::AGC_DIRECT_RESOURCE_TYPE_COUNT;
    FetchVertexHeader.semantic.hardware_mapping = 4;
    FetchVertexHeader.semantic.size_in_elements = 4;
    FetchVertexHeader.shader.input_semantics = &FetchVertexHeader.semantic;
    FetchVertexHeader.shader.num_input_semantics = 1;
    Registered(FetchVertexHeader, FetchVertexCode.data(), sizeof(FetchVertexCode), 2);
    Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
}

AgcDriver::DriverDetail::DrawEntryCounters FetchDraw(const FetchTables& tables) {
    alignas(16) static constexpr std::array<float, 4> source{1.0f, 0.0f, 0.0f, 0.0f};
    auto commands = DrawFrom({source.data()}, FetchVertexCode.data(), &tables);
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "the draw submission failed");
    AgcDriverWaitIdle_nid_postfix();
    return AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
}

void MovedVertexFetchTablesHitTheCache(bool libraries, bool relocation) {
    RegisterFetchVertex();
    const auto mode = std::string(libraries ? "with" : "without") + " graphics pipeline libraries" + (relocation ? "" : ", relocation disabled");
    static std::array<FetchTables, 8> same{};
    for (auto& tables : same) tables = Tables(16u);
    static auto restrided = Tables(32u);
    static auto reformatted = Tables(16u);
    reformatted.attrib[0] = ((77u << 2u) | 2u) << 5u;
    const auto start = AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    const auto since = [&](const AgcDriver::DriverDetail::DrawEntryCounters& counters) {
        return std::to_string(counters.hits - start.hits) + " hits and " + std::to_string(counters.inserts - start.inserts) + " inserts in " + std::to_string(counters.lookups - start.lookups) + " lookups";
    };
    AgcDriver::DriverDetail::DrawEntryCounters counters{};
    for (std::size_t i = 0; i < 4; ++i) counters = FetchDraw(same[i]);
    Require(counters.lookups - start.lookups == 4 && counters.hits == start.hits, mode + ": the draws that teach the moved vertex fetch tables hit the cache (" + since(counters) + ")");
    const auto taught = counters;
    for (std::size_t i = 4; i < same.size(); ++i) counters = FetchDraw(same[i]);
    if (relocation) Require(counters.hits - start.hits == 4 && taught.inserts - start.inserts == 2 && counters.inserts == taught.inserts, mode + ": draws whose vertex fetch tables moved to identical contents did not reuse the cached draw (" + since(counters) + ")");
    else Require(counters.hits == start.hits, mode + ": draws whose vertex fetch tables moved reused the cached draw (" + since(counters) + ")");
    const auto moved = counters;
    counters = FetchDraw(restrided);
    Require(counters.lookups == moved.lookups + 1 && counters.hits == moved.hits, mode + ": a draw whose moved vertex buffer table holds another stride reused the cached draw (" + since(counters) + ")");
    counters = FetchDraw(reformatted);
    Require(counters.lookups == moved.lookups + 2 && counters.hits == moved.hits, mode + ": a draw whose moved vertex attribute table holds another format reused the cached draw (" + since(counters) + ")");
    counters = FetchDraw(same[5]);
    if (relocation) Require(counters.hits == moved.hits + 1, mode + ": a draw at vertex fetch tables seen before did not reuse the cached draw (" + since(counters) + ")");
    same[5].buffer[1] = (same[5].buffer[1] & 0xffffu) | (48u << 16u);
    const auto before = counters;
    counters = FetchDraw(same[5]);
    Require(counters.hits == before.hits, mode + ": a draw whose vertex buffer table was rewritten in place reused the cached draw (" + since(counters) + ")");
}

}

int main(int argc, char** argv) {
    try {
        bool libraries = false;
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
            libraries = device->GraphicsPipelineLibraries();
        }
        if (!libraries && std::getenv("APS5_NO_GPL") == nullptr) return VulkanTestSkipped;
        if (argc > 1 && std::string(argv[1]) == "--without-relocation") {
            Require(std::getenv("APS5_NO_DRAW_RELOCATION") != nullptr, "--without-relocation needs APS5_NO_DRAW_RELOCATION");
            MovedVertexFetchTablesHitTheCache(libraries, false);
            AgcDriverShutdown_nid_postfix();
            std::cout << "A draw whose vertex fetch tables moved did not reuse the cached draw without relocation\n";
            return 0;
        }
        MovedDataHitsTheCache(libraries);
        UnrelocatedAliasesStayOut(libraries);
        MovedVertexFetchTablesHitTheCache(libraries, true);
        AgcDriverShutdown_nid_postfix();
        std::cout << "A draw whose data moved reused the cached draw " << (libraries ? "with" : "without") << " graphics pipeline libraries\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
