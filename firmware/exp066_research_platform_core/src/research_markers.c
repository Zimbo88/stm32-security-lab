#include <stdint.h>

#define RDP2_MARKER_SECTION(name) \
    __attribute__((used, section(name), aligned(1)))

#if RDP2_SLOT_A
const uint8_t rdp2_slot_marker[] RDP2_MARKER_SECTION(".rdp2_marker.slot") =
    "RDP2-SLOT-A-MARKER-01";
#else
const uint8_t rdp2_slot_marker[] RDP2_MARKER_SECTION(".rdp2_marker.slot") =
    "RDP2-SLOT-B-MARKER-01";
#endif

static const uint8_t rdp2_sram_marker_value[] = "RDP2-SRAM-MARKER-01";

volatile uint8_t rdp2_sram_marker[sizeof(rdp2_sram_marker_value)]
    __attribute__((used, section(".noinit.rdp2_marker"), aligned(1))) = {0};

void rdp2_research_markers_init(void)
{
    for (uint32_t index = 0U; index < sizeof(rdp2_sram_marker_value); ++index) {
        rdp2_sram_marker[index] = rdp2_sram_marker_value[index];
    }
}
