#pragma once
#include <cstdint>
struct ProbeReport {
    std::uint32_t magic;
    std::uint32_t status;
    std::uint32_t jniVersion;
    std::uint32_t vmCount;
    std::uint32_t dosSignature;
    std::uint32_t peSignature;
    std::uint32_t machine;
    std::uint32_t attachedHere;
    std::uint32_t detached;
};
static_assert(sizeof(ProbeReport) == 36);
constexpr std::uint32_t reportMagic = 0x4C4C4142;
struct MetadataReport {
    std::uint32_t magic, status, matches, truncated;
    char query[512]; // Empty: game class inventory. Otherwise exact JVM signature.
    char text[131072]; // Class/member names and descriptors only; never field values.
};
