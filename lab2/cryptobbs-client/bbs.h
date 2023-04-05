#include <cstdint>
#include <devctl.h>
#include <sys/types.h>
#include <unistd.h>

#define SET_GEN_PARAMS __DIOT(_DCMD_MISC, 1, std::uint32_t)
#define GET_ELEMENT __DIOF(_DCMD_MISC, 2, bbs::BBSParams)

namespace bbs {

struct BBSParams
{
    std::uint32_t seed;
    std::uint32_t p;
    std::uint32_t q;
};
}
