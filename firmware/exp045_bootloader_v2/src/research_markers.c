#include <stdint.h>

#define RDP2_MARKER_SECTION(name) \
    __attribute__((used, section(name), aligned(1)))

const uint8_t rdp2_boot_marker[] RDP2_MARKER_SECTION(".rdp2_marker.boot") =
    "RDP2-BOOT-MARKER-01";
