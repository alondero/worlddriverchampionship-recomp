// Bootstrap entrypoint. This intentionally builds a named executable before
// ROM analysis is complete. Replace it with the real SDL/runtime bridge after
// recomp_entrypoint, VI, input, audio, and renderer contracts are measured.

#include <cstdio>

int main() {
    std::fprintf(stderr,
        "World Driver Championship: Recompiled bootstrap; ROM-specific port not initialized.\n");
    return 0;
}
