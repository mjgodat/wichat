// RTL8811AU channel tuning tables - extracted from USBPcap
// Auto-generated - do not edit

#ifndef RTL8811AU_CHANNELS_H
#define RTL8811AU_CHANNELS_H

#include <stdint.h>

typedef struct {
    uint8_t  channel;
    uint16_t freq_mhz;
    uint8_t  band_5ghz;
    uint32_t rf18_val;
} rtl_channel_t;

#define RTL_NUM_CHANNELS 37

static const rtl_channel_t rtl_channels[] = {
    {  1,  2412, 0, 0x07C01},
    {  2,  2417, 0, 0x07C02},
    {  3,  2422, 0, 0x07C03},
    {  4,  2427, 0, 0x07C04},
    {  5,  2432, 0, 0x07C05},
    {  6,  2437, 0, 0x07C06},
    {  7,  2442, 0, 0x07C07},
    {  8,  2447, 0, 0x07C08},
    {  9,  2452, 0, 0x07C09},
    { 10,  2457, 0, 0x0700A},
    { 11,  2462, 0, 0x07C0B},
    { 12,  2467, 0, 0x07C0C},
    { 13,  2472, 0, 0x07C0D},
    { 36,  5180, 1, 0x17D24},
    { 40,  5200, 1, 0x57D28},
    { 44,  5220, 1, 0x17D2C},
    { 48,  5240, 1, 0x17D30},
    { 52,  5260, 1, 0x57D34},
    { 56,  5280, 1, 0x17D38},
    { 60,  5300, 1, 0x17D3C},
    { 64,  5320, 1, 0x17D40},
    {100,  5500, 1, 0x17D64},
    {104,  5520, 1, 0x37D68},
    {108,  5540, 1, 0x37D6C},
    {112,  5560, 1, 0x37D70},
    {116,  5580, 1, 0x37D74},
    {120,  5600, 1, 0x37D78},
    {124,  5620, 1, 0x37D7C},
    {128,  5640, 1, 0x37D80},
    {132,  5660, 1, 0x37D84},
    {136,  5680, 1, 0x37D88},
    {140,  5700, 1, 0x37D8C},
    {149,  5745, 1, 0x17D95},
    {153,  5765, 1, 0x17D99},
    {157,  5785, 1, 0x57D9D},
    {161,  5805, 1, 0x57DA1},
    {165,  5825, 1, 0x57DA5},
};

// Non-overlapping 2.4GHz channels for mesh flood
#define RTL_MESH_24G_CHANNELS {1, 6, 11}
#define RTL_MESH_24G_COUNT 3

// Non-DFS 5GHz channels for mesh flood
#define RTL_MESH_5G_CHANNELS {36, 40, 44, 48, 149, 153, 157, 161, 165}
#define RTL_MESH_5G_COUNT 9

#ifndef RTL_REG_WRITE_T_DEFINED
#define RTL_REG_WRITE_T_DEFINED
typedef struct { uint16_t reg; uint8_t len; uint32_t val; } rtl_reg_write_t;
#endif

// Same-band channel switch (2 writes)
static const rtl_reg_write_t rtl_switch_same_band[] = {
    {0x0C90, 4, 0x01807C0A},
    {0x08B0, 4, 0x00000018},
};
#define RTL_SWITCH_SAME_BAND_COUNT 2

// Band-crossing switch (98 writes)
static const rtl_reg_write_t rtl_switch_cross_band[] = {
    {0x0C90, 4, 0x01800006},
    {0x0C90, 4, 0x0EF02000},
    {0x0C90, 4, 0x03B3824B},
    {0x0C90, 4, 0x03B3024B},
    {0x0C90, 4, 0x03B2844B},
    {0x0C90, 4, 0x03B20F4B},
    {0x0C90, 4, 0x03B18F4B},
    {0x0C90, 4, 0x03B104B2},
    {0x0C90, 4, 0x03B08049},
    {0x0C90, 4, 0x03B00148},
    {0x0C90, 4, 0x03B7824B},
    {0x0C90, 4, 0x03B7024B},
    {0x0C90, 4, 0x03B6824B},
    {0x0C90, 4, 0x03B60F4B},
    {0x0C90, 4, 0x03B58F4B},
    {0x0C90, 4, 0x03B504B2},
    {0x0C90, 4, 0x03B48049},
    {0x0C90, 4, 0x03B40148},
    {0x0C90, 4, 0x0EF00000},
    {0x0C90, 4, 0x0EF00100},
    {0x0C90, 4, 0x0340ADF3},
    {0x0C90, 4, 0x03409DF0},
    {0x0C90, 4, 0x03408D70},
    {0x0C90, 4, 0x03407D6D},
    {0x0C90, 4, 0x03406CEE},
    {0x0C90, 4, 0x03405CCC},
    {0x0C90, 4, 0x034044EC},
    {0x0C90, 4, 0x034034AC},
    {0x0C90, 4, 0x0340246D},
    {0x0C90, 4, 0x0340106F},
    {0x0C90, 4, 0x0340006C},
    {0x0C90, 4, 0x0EF00000},
    {0x0C90, 4, 0x0ED00010},
    {0x0C90, 4, 0x0440ADF2},
    {0x0C90, 4, 0x04409DEF},
    {0x0C90, 4, 0x04408DEC},
    {0x0C90, 4, 0x04407DE9},
    {0x0C90, 4, 0x04406CEC},
    {0x0C90, 4, 0x04405CE9},
    {0x0C90, 4, 0x044044EC},
    {0x0C90, 4, 0x044034E9},
    {0x0C90, 4, 0x0440246C},
    {0x0C90, 4, 0x04401469},
    {0x0C90, 4, 0x0440006C},
    {0x0C90, 4, 0x0ED00000},
    {0x0C90, 4, 0x0ED00001},
    {0x0C90, 4, 0x04038DA7},
    {0x0C90, 4, 0x040300C2},
    {0x0C90, 4, 0x040288E2},
    {0x0C90, 4, 0x040200B8},
    {0x0C90, 4, 0x040188A5},
    {0x0C90, 4, 0x04010FBC},
    {0x0C90, 4, 0x04008F71},
    {0x0C90, 4, 0x04000240},
    {0x0C90, 4, 0x0ED00000},
    {0x0C90, 4, 0x0EF020A2},
    {0x0C90, 4, 0x0DF00080},
    {0x0C90, 4, 0x03500120},
    {0x0C90, 4, 0x03508120},
    {0x0C90, 4, 0x03510120},
    {0x0C90, 4, 0x03600085},
    {0x0C90, 4, 0x03608085},
    {0x0C90, 4, 0x03610085},
    {0x0C90, 4, 0x03618085},
    {0x0C90, 4, 0x0EF00000},
    {0x0C90, 4, 0x05100C31},
    {0x0C90, 4, 0x05200622},
    {0x0C90, 4, 0x053FC70B},
    {0x0C90, 4, 0x0540017E},
    {0x0C90, 4, 0x05651DF3},
    {0x0C90, 4, 0x05100C01},
    {0x0C90, 4, 0x052006D6},
    {0x0C90, 4, 0x053FC649},
    {0x0C90, 4, 0x07049661},
    {0x0C90, 4, 0x0717843E},
    {0x0C90, 4, 0x07200382},
    {0x0C90, 4, 0x07451400},
    {0x0C90, 4, 0x03500160},
    {0x0C90, 4, 0x03508160},
    {0x0C90, 4, 0x03510160},
    {0x0C90, 4, 0x03600124},
    {0x0C90, 4, 0x03608124},
    {0x0C90, 4, 0x03610124},
    {0x0C90, 4, 0x03618124},
    {0x0C90, 4, 0x0ED0000C},
    {0x0C90, 4, 0x04500140},
    {0x0C90, 4, 0x04508140},
    {0x0C90, 4, 0x04510140},
    {0x0C90, 4, 0x04600124},
    {0x0C90, 4, 0x04608124},
    {0x0C90, 4, 0x04610124},
    {0x0C90, 4, 0x04618124},
    {0x0C90, 4, 0x0DF00088},
    {0x0C90, 4, 0x0B3F0E18},
    {0x0C90, 4, 0x0B41214C},
    {0x0C90, 4, 0x0B73000C},
    {0x0C90, 4, 0x01C539D2},
    {0x0C90, 4, 0x0C4AFE00},
};
#define RTL_SWITCH_CROSS_BAND_COUNT 98

// RF indirect write: val = (rf_reg << 20) | (data & 0xFFFFF)
#define RTL_RF_WRITE(rf_reg, data) (((uint32_t)(rf_reg) << 20) | ((data) & 0xFFFFF))
#define RTL_RF_ADDR(val) (((val) >> 20) & 0xFF)
#define RTL_RF_DATA(val) ((val) & 0xFFFFF)
#define RTL_RF_PORT 0x0C90

#endif // RTL8811AU_CHANNELS_H
