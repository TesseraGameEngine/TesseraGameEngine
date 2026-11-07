#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct BundleTrailer {
    uint64_t payloadOffset{0};
    uint64_t payloadSize{0};
    char magic[8]{ 'A', 'G', 'Y', 'P', 'A', 'C', 'K', '1' };
};

struct BundleHeader {
    char magic[8]{ 'A', 'G', 'Y', 'P', 'A', 'C', 'K', '1' };
    uint32_t fileCount{0};
    uint32_t flags{0};
    char mainExeName[128]{0};
};
#pragma pack(pop)

