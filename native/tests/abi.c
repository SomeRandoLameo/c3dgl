#include "c3dgl_native.h"
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void) {
    uint32_t output[4] = {0}, values[2] = {0x12345678, 0xabcdef};
    CHECK(c3dglNativeEncodeCommand(output, 4, 0x200, 15, true, values, 2) == 4);
    CHECK(output[0] == values[0] && output[1] == 0x801f0200);
    CHECK(output[2] == values[1] && output[3] == 0);
    CHECK(c3dglNativeEncodeCommand(output, 1, 0x200, 15, false, values, 1) == 0);
    CHECK(output[0] == values[0]);
    return 0;
}
