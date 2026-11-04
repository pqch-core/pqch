// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// PQCH: tests for the ASERT (aserti3) difficulty adjustment.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/util/setup_common.h>
#include <tinyformat.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

namespace {
struct AsertVector {
    uint32_t ref_bits;
    int64_t time_diff;
    int64_t height_diff;
    uint32_t expected_bits;
};

const AsertVector ASERT_VECTORS[] = {
// Gerado por contrib/pqch/asert_reference.py vectors (seed 20261104). Nao editar a mao.
// {ref_bits, time_diff, height_diff, expected_bits}
    {0x1d00ffff, 60, 0, 0x1d00ffff},
    {0x1d00ffff, 14460, 0, 0x1d00ffff},
    {0x1d00ffff, -14340, 0, 0x1c7fff80},
    {0x1b0404cb, 14460, 0, 0x1b080996},
    {0x1b0404cb, -43140, 0, 0x1b008099},
    {0x1d00ffff, 120, 1, 0x1d00ffff},
    {0x1d00ffff, 14520, 1, 0x1d00ffff},
    {0x1d00ffff, -14280, 1, 0x1c7fff80},
    {0x1b0404cb, 14520, 1, 0x1b080996},
    {0x1b0404cb, -43080, 1, 0x1b008099},
    {0x1d00ffff, 180, 2, 0x1d00ffff},
    {0x1d00ffff, 14580, 2, 0x1d00ffff},
    {0x1d00ffff, -14220, 2, 0x1c7fff80},
    {0x1b0404cb, 14580, 2, 0x1b080996},
    {0x1b0404cb, -43020, 2, 0x1b008099},
    {0x1d00ffff, 14400, 239, 0x1d00ffff},
    {0x1d00ffff, 28800, 239, 0x1d00ffff},
    {0x1d00ffff, 0, 239, 0x1c7fff80},
    {0x1b0404cb, 28800, 239, 0x1b080996},
    {0x1b0404cb, -28800, 239, 0x1b008099},
    {0x1d00ffff, 14460, 240, 0x1d00ffff},
    {0x1d00ffff, 28860, 240, 0x1d00ffff},
    {0x1d00ffff, 60, 240, 0x1c7fff80},
    {0x1b0404cb, 28860, 240, 0x1b080996},
    {0x1b0404cb, -28740, 240, 0x1b008099},
    {0x1d00ffff, 14520, 241, 0x1d00ffff},
    {0x1d00ffff, 28920, 241, 0x1d00ffff},
    {0x1d00ffff, 120, 241, 0x1c7fff80},
    {0x1b0404cb, 28920, 241, 0x1b080996},
    {0x1b0404cb, -28680, 241, 0x1b008099},
    {0x1d00ffff, 86460, 1440, 0x1d00ffff},
    {0x1d00ffff, 100860, 1440, 0x1d00ffff},
    {0x1d00ffff, 72060, 1440, 0x1c7fff80},
    {0x1b0404cb, 100860, 1440, 0x1b080996},
    {0x1b0404cb, 43260, 1440, 0x1b008099},
    {0x1d00ffff, 31557660, 525960, 0x1d00ffff},
    {0x1d00ffff, 31572060, 525960, 0x1d00ffff},
    {0x1d00ffff, 31543260, 525960, 0x1c7fff80},
    {0x1b0404cb, 31572060, 525960, 0x1b080996},
    {0x1b0404cb, 31514460, 525960, 0x1b008099},
    {0x1d00ffff, 141360060, 2356000, 0x1d00ffff},
    {0x1d00ffff, 141374460, 2356000, 0x1d00ffff},
    {0x1d00ffff, 141345660, 2356000, 0x1c7fff80},
    {0x1b0404cb, 141374460, 2356000, 0x1b080996},
    {0x1b0404cb, 141316860, 2356000, 0x1b008099},
    {0x1b0404cb, -1000000000, 1000000, 0x01010000},
    {0x1b0404cb, 1000000000, 0, 0x1d00ffff},
    {0x03000001, -1000000, 10000, 0x01010000},
    {0x1b0404cb, 60, 0, 0x1b0404cb},
    {0x1b0404cb, 960, 0, 0x1b043271},
    {0x1b0404cb, 1861, 0, 0x1b046212},
    {0x1b0404cb, 2761, 0, 0x1b0493b9},
    {0x1b0404cb, 3662, 0, 0x1b04c797},
    {0x1b0404cb, 4563, 0, 0x1b04fdc3},
    {0x1b0404cb, 5463, 0, 0x1b053647},
    {0x1b0404cb, 6364, 0, 0x1b057165},
    {0x1b0404cb, 7265, 0, 0x1b05af27},
    {0x1b0404cb, 8165, 0, 0x1b05ef9c},
    {0x1b0404cb, 9066, 0, 0x1b063308},
    {0x1b0404cb, 9967, 0, 0x1b067970},
    {0x1b0404cb, 10867, 0, 0x1b06c2e8},
    {0x1b0404cb, 11768, 0, 0x1b070faf},
    {0x1b0404cb, 12669, 0, 0x1b075fd3},
    {0x1b0404cb, 13569, 0, 0x1b07b357},
    {0x1d009761, 229497734, 3825907, 0x1c09dbf4},
    {0x1d00de84, 161334765, 2688330, 0x1d00ffff},
    {0x1d00ff00, 329329560, 5361411, 0x1d00ffff},
    {0x1d00bd40, 47202, 426, 0x1d00ffff},
    {0x1c7e02dd, 14793992, 200189, 0x1d00ffff},
    {0x1d00fc9d, 9953742, 208087, 0x070121f2},
    {0x1c0a3aae, -6143, 107, 0x1c05926f},
    {0x1c0140e3, 259507058, 4325938, 0x1b1dedf4},
    {0x1d00a0e8, 242151097, 4036324, 0x1c29013f},
    {0x1d00a18a, 8144787, 389, 0x1d00ffff},
    {0x1d009407, 104713656, 1799247, 0x01010000},
    {0x1d008ab2, 317094344, 5284649, 0x1d00ffff},
    {0x1d00fb9b, 43945, 629, 0x1d00ffff},
    {0x1c02a2a1, -833229, 139, 0x147bc286},
    {0x1d00b515, 447289527, 7525241, 0x01010000},
    {0x1c069397, 199825203, 3350359, 0x1200c6af},
    {0x1c0252cc, 320449259, 5341077, 0x1c011b29},
    {0x1c27dd12, -4198839, 612, 0x01010000},
    {0x1d00845b, 35015, 667, 0x1c67b801},
    {0x1c3f23d2, 60170676, 1001886, 0x1d00ffff},
    {0x1d00d872, 281509600, 4852689, 0x01010000},
    {0x1d00ff8d, 2508973, 180, 0x1d00ffff},
    {0x1c3fa282, 1434361, 181, 0x1d00ffff},
    {0x1c3fcaef, 6269130, 298, 0x1d00ffff},
    {0x1c5e7e21, 442174711, 7368889, 0x1d00ffff},
    {0x1c3cf724, -50036, 4, 0x1c0567a4},
    {0x1d008bbc, 3725255, 61282, 0x1d00ffff},
    {0x1c311fb5, 584, 356, 0x1c120476},
    {0x1d00cda2, -7579645, 206, 0x01010000},
    {0x1d00c1c4, 368984386, 6149832, 0x1d009405},
    {0x1c23d19f, -9412893, 277, 0x01010000},
    {0x1d00f06d, 253075233, 4218807, 0x1c12876b},
    {0x1c05a936, 62825, 961, 0x1c073ccc},
    {0x1d00d955, 579459535, 9657791, 0x1d0093f7},
    {0x1d00b9a7, 369533631, 6005974, 0x1d00ffff},
    {0x1d00a02f, -43437, 133, 0x1c0d712b},
    {0x1d0096b3, -5692314, 380, 0x01010000},
    {0x1c5d791e, 36212, 277, 0x1d00ef51},
    {0x1d0091cc, -23604, 394, 0x1c0ef55f},
    {0x1c0e0ce3, 58729, 323, 0x1c5d1e60},
    {0x1d008f6c, 214916172, 3522400, 0x1d00ffff},
    {0x1d00f407, 480980312, 7854892, 0x1d00ffff},
    {0x1d00b4dd, 366995544, 6116244, 0x1d00ffff},
    {0x1d00c8dc, 398516729, 6642415, 0x1c339cfa},
    {0x1c320c90, 8536159, 520, 0x1d00ffff},
    {0x1d00b09c, 95825880, 1597189, 0x1d008765},
    {0x1d00a48d, 7591495, 167, 0x1d00ffff},
    {0x1d00e8cd, -5038, 715, 0x1c171966},
    {0x1c2d73c5, 304299450, 5071043, 0x1d00ffff},
    {0x1c22331f, 265561147, 4426533, 0x1c07bb3d},
    {0x1c55de38, 13867081, 230566, 0x1d00ffff},
    {0x1d00e71c, 40025, 581, 0x1d00ffff},
    {0x1d00a53d, 64567, 719, 0x1d00ffff},
    {0x1d00b2f4, -8026466, 196, 0x01010000},
    {0x1d00c42e, 419565920, 6928236, 0x1d00ffff},
    {0x1d00949a, 248607401, 4142983, 0x1d00ffff},
    {0x1d00e700, 75117, 582, 0x1d00ffff},
    {0x1c29cfa2, -7271323, 86, 0x01010000},
    {0x1c2740ce, -423274, 836, 0x18154f09},
    {0x1d00a336, 368088783, 6134049, 0x1d00ffff},
    {0x1c6d2db9, 132176515, 2092653, 0x1d00ffff},
    {0x1c6619d8, 419063122, 6984871, 0x1c190ac5},
    {0x1c51891f, 134636129, 2150483, 0x1d00ffff},
    {0x1d00fe3b, 8890910, 706, 0x1d00ffff},
    {0x1d009002, 405553127, 6759473, 0x1c44e9f1},
    {0x1d008a19, 279347174, 4677533, 0x11168799},
    {0x1c7e940e, 587306434, 9865608, 0x01010000},
    {0x1d00833c, 13289, 318, 0x1c6304bd},
    {0x1c76ab92, 438457632, 7388287, 0x01010000},
    {0x1d00bf07, -33153, 275, 0x1c117380},
    {0x1c5d7f0a, 83051447, 1383936, 0x1d00c29a},
    {0x1d00e202, 63496, 650, 0x1d00ffff},
    {0x1d00aab0, 191509731, 3191688, 0x1d00ff9d},
    {0x1d009b52, 81307, 908, 0x1d00ffff},
    {0x1d0095fd, 227136954, 3785917, 0x1c3eaf6d},
    {0x1d00c347, -6109, 375, 0x1c312114},
    {0x1d0087fa, 67698, 474, 0x1d00ffff},
    {0x1d00d202, 16820, 718, 0x1c3b29fe},
    {0x1c03b2ca, 249238544, 4024759, 0x1d00ffff},
    {0x1c786fb9, 159817329, 2664230, 0x1c14c060},
    {0x1c08da8a, 12609, 297, 0x1c06de9c},
    {0x1c58928d, 59548995, 1013773, 0x11361767},
    {0x1d009bee, 18648, 686, 0x1c349b0e},
    {0x1d00bba0, -1913, 396, 0x1c365f3f},
    {0x1d008b66, 490946671, 8182473, 0x1d008009},
    {0x1d00c442, -9883, 263, 0x1c38e695},
    {0x1d00bff0, 2889095, 45, 0x1d00ffff},
    {0x1c6af049, 201787222, 3363168, 0x1c5cef20},
    {0x1d00ddee, 193174559, 3219332, 0x1d00ffff},
    {0x1c0df462, -1115, 940, 0x1c00df8b},
    {0x1d009773, 10828, 242, 0x1c7e6ef2},
    {0x1d00beaa, 506037561, 8365391, 0x1d00ffff},
    {0x1d00b35d, -25324, 382, 0x1c118961},
    {0x1d00e76a, 58688, 712, 0x1d00ffff},
    {0x1d00a4e3, 83121, 454, 0x1d00ffff},
    {0x1c4495ac, 80077, 912, 0x1d00e7c8},
    {0x1c042225, 2916389, 727, 0x1d00ffff},
    {0x1d00febd, 561121703, 9351374, 0x1d00ffff},
    {0x1c17bdb8, 560673213, 9344911, 0x1c086e48},
    {0x1d008b39, 346659339, 5777241, 0x1d00ffff},
    {0x1c742da7, 102000881, 1802641, 0x01010000},
    {0x1c187cfd, -6164213, 727, 0x01010000},
    {0x1c5d3994, 558877375, 9278215, 0x1d00ffff},
    {0x1c43eb95, 281819468, 4696241, 0x1d00ffff},
    {0x1c12cd60, 51920, 105, 0x1d00a887},
    {0x1d0080f9, 8431373, 398, 0x1d00ffff},
    {0x1c710726, 551696890, 9264182, 0x01010000},
    {0x1d00e53a, 416623965, 6943399, 0x1d00ffff},
    {0x1c3338e3, 317033529, 5268119, 0x1d00ffff},
    {0x1c262ce3, -1554693, 434, 0x0f00b233},
    {0x1d00998a, 519949211, 8766687, 0x01010000},
    {0x1c01faca, -4022052, 567, 0x01010000},
    {0x1d00b20c, 264629406, 4411128, 0x1c1c20fc},
    {0x1d00fcec, 9082003, 310, 0x1d00ffff},
    {0x1c5e1e87, 570401958, 9473882, 0x1d00ffff},
    {0x1d0083f9, 338481610, 5726902, 0x01010000},
    {0x1d009b2b, 586837330, 9780759, 0x1c683477},
    {0x1c0e01e0, -7865955, 971, 0x01010000},
    {0x1c704596, 21982, 841, 0x1c1c6cf2},
    {0x1d00de61, 6258122, 720, 0x1d00ffff},
    {0x1d008d5b, 319711781, 5272096, 0x1d00ffff},
    {0x1c4db0e2, 190350270, 3173344, 0x1c06db45},
    {0x1c1d4791, -5401703, 336, 0x01010000},
    {0x1d00f932, 105174579, 1753223, 0x1c64860a},
    {0x1d00f12b, 7306133, 543, 0x1d00ffff},
    {0x1d00cafc, 1479, 421, 0x1c406cad},
    {0x1d00f14b, 8029653, 631, 0x1d00ffff},
    {0x1c26c543, 2114339, 54, 0x1d00ffff},
    {0x1c55eae0, 7308120, 934, 0x1d00ffff},
    {0x1d00eefe, 66203, 870, 0x1d00ffff},
    {0x1d00da6d, 3611827, 385, 0x1d00ffff},
    {0x1c3953fa, -6723546, 661, 0x01010000},
    {0x1c77c23f, -9676, 105, 0x1c3759e5},
    {0x1d00dd32, -1737363, 968, 0x0d08952e},
    {0x1d00d1dd, 401456198, 6644908, 0x1d00ffff},
    {0x1d00e942, 34412, 872, 0x1c623b32},
    {0x1d00ddc8, 418794, 62, 0x1d00ffff},
    {0x1d00e56d, 23661218, 393949, 0x1d00ffff},
    {0x1c3a2902, 42314, 755, 0x1c323bc3},
    {0x1d00be7c, 6749238, 164, 0x1d00ffff},
    {0x1c4169bf, 575843501, 9597222, 0x1c6a7a16},
    {0x1c4aab9c, 30337830, 350796, 0x1d00ffff},
    {0x1d009033, 527773625, 8795820, 0x1d00ffff},
    {0x1c3e7e71, 114833096, 1914297, 0x1c12f49d},
    {0x1c175827, 307590729, 5127152, 0x1c03aad8},
    {0x1c500e84, -1728183, 483, 0x0d139c96},
    {0x1c36b81c, -27757, 405, 0x1c0473df},
    {0x1c1b925c, 476768196, 7969357, 0x10105421},
    {0x1c364a6d, 376565157, 6196112, 0x1d00ffff},
    {0x1c68d550, 5526705, 882, 0x1d00ffff},
    {0x1c4f3a02, 127877634, 2131386, 0x1c3c8bc9},
    {0x1c2ecf90, 466501896, 7775229, 0x1c1a654d},
    {0x1c211d7c, 53790, 571, 0x1c54883f},
    {0x1c4d1c9f, 39855, 651, 0x1c4fe54d},
    {0x1c6c1101, 495186123, 8253173, 0x1c57cb12},
    {0x1c3e1a93, -19846, 49, 0x1c14ad61},
    {0x1c5d46ca, 337804824, 5762938, 0x01010000},
    {0x1c682a06, -7502889, 25334, 0x01010000},
    {0x1d00bcae, -15655, 564, 0x1c115e51},
    {0x1d0097dc, 9528282, 901, 0x1d00ffff},
    {0x1c443571, 327492283, 5540885, 0x01010000},
    {0x1c4939d3, 385090248, 6418514, 0x1c1b18b8},
    {0x1d00f788, 360673219, 6069120, 0x01010000},
    {0x1d008b08, -6449, 4, 0x1c6475cb},
    {0x1c432ccc, 306721427, 5112747, 0x1c084ba7},
    {0x1d00cd69, 270280462, 4503731, 0x1d00ffff},
    {0x1c27d9bb, 109286690, 1953284, 0x01010000},
    {0x1d00ac54, 300427189, 5006579, 0x1d00ffff},
    {0x1d00b9dc, 9496, 958, 0x1c126699},
    {0x1d0091d6, 540942310, 9015789, 0x1c722505},
    {0x1d00f749, 187925315, 3185431, 0x01030000},
    {0x1c31c711, 4528, 816, 0x1c05d8fc},
    {0x1c4d5392, -4207063, 714, 0x01010000},
    {0x1d009c21, 3063768, 922, 0x1d00ffff},
    {0x1c46d295, 35988, 902, 0x1c1d8153},
    {0x1d00cc34, 233001915, 3882851, 0x1d00ffff},
    {0x1d00e62a, 97426399, 1720500, 0x01010000},
    {0x1c627005, 3905033, 65, 0x1d00ffff},
    {0x1d00dce6, 130325645, 2173041, 0x1c0e4c31},
    {0x1d00c5ac, -52359, 575, 0x1c030338},
    {0x1c4c39de, -3201568, 980, 0x01010000},
    {0x1d00c47c, 132913087, 2312358, 0x01010000},
    {0x1d008506, 13685645, 187711, 0x1d00ffff},
    {0x1d00aa03, 556425422, 9273782, 0x1d009dbf},
    {0x1d00e29a, 78848, 805, 0x1d00ffff},
    {0x1c7ab0c8, 482701622, 8045912, 0x1c097f04},
    {0x1c2513d5, 299191898, 5124456, 0x01010000},
    {0x1d0098a8, -16179, 48, 0x1c3cd1be},
    {0x1d00fdad, 208662039, 3429167, 0x1d00ffff},
    {0x1c22ff1a, 235064598, 3931174, 0x1523d925},
    {0x1c2145ef, 519982423, 8666385, 0x1c201da8},
    {0x1d009989, 431578046, 7193682, 0x1c13709f},
    {0x1d00ade2, -22361, 11, 0x1c393e36},
    {0x1d00cd9e, -2571, 206, 0x1c63eeac},
    {0x1c036dcf, 6322491, 325, 0x1d00ffff},
    {0x1c12da61, 222728875, 3713065, 0x1c015480},
    {0x1c619251, 213575238, 3683488, 0x01010000},
    {0x1d00ded1, 553311752, 9074591, 0x1d00ffff},
    {0x1c64189a, 56437, 9, 0x1d00ffff},
    {0x1c0fcde0, 559455365, 9402569, 0x01010000},
    {0x1c242b30, 572479208, 9508967, 0x1d00ffff},
    {0x1d009b14, -5771229, 691, 0x01010000},
    {0x1c379f5a, 458944919, 7640776, 0x1d00ffff},
    {0x1d0083c6, -4506866, 464, 0x01010000},
    {0x1c74e663, -7502341, 416, 0x01010000},
    {0x1c62e052, 23829, 478, 0x1c4e0f6e},
    {0x1c48f57e, 404265134, 6651291, 0x1d00ffff},
    {0x1c7a7d95, 437122709, 7328362, 0x060e442b},
    {0x1d00c4e1, 93217057, 1706856, 0x01010000},
    {0x1d00fa0a, 475388861, 7802437, 0x1d00ffff},
    {0x1c4e3db4, 474836064, 7913007, 0x1d00ffff},
    {0x1d009184, 388472320, 6474750, 0x1c4ed180},
    {0x1d00e189, -7945868, 489, 0x01010000},
    {0x1c2acc0d, 314420821, 5393131, 0x01010000},
    {0x1c6be349, 227893569, 3929957, 0x01010000},
    {0x1d00ad88, 62660493, 1023176, 0x1d00ffff},
    {0x1d00c22b, 6705921, 22, 0x1d00ffff},
    {0x1d00d8cc, 57755, 380, 0x1d00ffff},
    {0x1d00cf0e, 67136, 913, 0x1d00ffff},
    {0x1d00ef1f, -8955775, 81, 0x01010000},
    {0x1d00d28e, 7465519, 141, 0x1d00ffff},
    {0x1c7d3bcf, 85722502, 1427970, 0x1d00ffff},
    {0x1d0084e1, 82718679, 1377973, 0x1d00ffff},
    {0x1c152b39, 8189103, 902, 0x1d00ffff},
    {0x1c7e0133, 74473, 866, 0x1d00ffff},
    {0x1c750eba, 3495566, 302, 0x1d00ffff},
    {0x1d00e88c, 14806, 684, 0x1c4198a7},
    {0x1d00bf13, 72897, 593, 0x1d00ffff},
    {0x1d009343, 3460691, 651, 0x1d00ffff},
    {0x1d0098c1, 565297834, 9541969, 0x01010000},
    {0x1c0c03dd, -5090, 549, 0x1c01ebae},
    {0x1c705c84, 392413171, 6595897, 0x01010000},
    {0x1d00e7f9, -2357332, 371, 0x080614b9},
    {0x1d00a2dd, 55712, 65, 0x1d00ffff},
    {0x1d0086f6, 33806, 66, 0x1d00ffff},
    {0x1d00f39f, -2335538, 944, 0x08037c13},
    {0x1c72ac5d, 45458, 127, 0x1d00ffff},
    {0x1c27f13d, 554178321, 9091468, 0x1d00ffff},
    {0x1d00845f, -9575583, 761, 0x01010000},
    {0x1d00c843, 452275230, 7526943, 0x1d00ffff},
    {0x1c694db9, -1908, 199, 0x1c35eacb},
    {0x1d00a82d, 257564550, 4401379, 0x01010000},
    {0x1c59a635, -43903, 45, 0x1c097c7f},
    {0x1d00b74d, 49131, 380, 0x1d00ffff},
    {0x1c284178, 213379129, 3597201, 0x0707ea27},
    {0x1d00869e, 595410, 469, 0x1d00ffff},
    {0x1c1c7a89, 34509194, 574779, 0x1c53ae9f},
    {0x1d00930e, 181920547, 3006790, 0x1d00ffff},
    {0x1d00c1da, -162107, 903, 0x1b017d4b},
    {0x1d00a21e, 1729931, 324, 0x1d00ffff},
    {0x1c4c20f3, 7717853, 428, 0x1d00ffff},
    {0x1c2aa5b4, -38165, 288, 0x1c02f2d5},
    {0x1d00c8ad, -5690359, 123, 0x01010000},
    {0x1c1a5e49, 8485063, 748, 0x1d00ffff},
    {0x1d00c3eb, 159024249, 2733474, 0x01010000},
    {0x1d00eec2, 39622, 273, 0x1d00ffff},
    {0x1d00ba09, 497612490, 8293035, 0x1d00ffff},
    {0x1c6d15be, 110743769, 1721486, 0x1d00ffff},
    {0x1c5bc49e, 39649, 249, 0x1d00ffff},
    {0x1c0cdfeb, 511789289, 8530767, 0x1c00d632},
    {0x1d00cd56, 55267, 8, 0x1d00ffff},
    {0x1d008546, 442182508, 7369923, 0x1c478642},
    {0x1d00c79d, 67356, 469, 0x1d00ffff},
    {0x1c159567, 4381049, 874, 0x1d00ffff},
    {0x1c713077, -3201448, 300, 0x01010000},
    {0x1d00d39a, 85720825, 1427816, 0x1d00ffff},
    {0x1d00a8be, -7957, 812, 0x1c0afea6},
    {0x1d00a417, 75402, 723, 0x1d00ffff},
    {0x1d0098be, 75359, 592, 0x1d00ffff},
    {0x1d008eea, -2947731, 740, 0x0300a532},
    {0x1c784093, 79519300, 1224676, 0x1d00ffff},
    {0x1c2886b5, 392662872, 6544050, 0x1c692a60},
    {0x1c1bf51c, 47534, 606, 0x1c2fbd1f},
    {0x1d00bd9d, 26556, 266, 0x1d00ffff},
    {0x1d0093dd, -9500989, 21, 0x01010000},
    {0x1d00dea6, 84212, 489, 0x1d00ffff},
    {0x1d00846a, 24047, 36, 0x1d00ffff},
    {0x1c3ede34, 292024208, 4866393, 0x1d00ffff},
    {0x1d0087a5, -4728, 838, 0x1c0993d5},
    {0x1d00f8f8, 348642546, 5751794, 0x1d00ffff},
    {0x1d009b6d, 43566, 922, 0x1c5805b3},
    {0x1d00c8eb, 458540917, 7642904, 0x1c28496d},
    {0x1c45db52, 594464849, 9907449, 0x1d00a4f3},
    {0x1c3e8808, -5867180, 333, 0x01010000},
    {0x1c22b4ed, 281111229, 4684281, 0x1d00ffff},
    {0x1d008aa0, 6565457, 326, 0x1d00ffff},
    {0x1d00bc1c, -37735, 140, 0x1c145bdc},
    {0x1d00b194, 438714604, 7333892, 0x110eb05b},
    {0x1c089701, 361750537, 6029670, 0x1c020de8},
    {0x1d00f174, 583870150, 9637445, 0x1d00ffff},
    {0x1d00b45c, 77190, 355, 0x1d00ffff},
    {0x1c4e689c, -2834161, 847, 0x033d83e2},
    {0x1d00e806, 235421822, 3924652, 0x1c0eac0c},
    {0x1c112508, 4182245, 68858, 0x1d00c4d2},
    {0x1d00d106, 401037539, 6769739, 0x01010000},
    {0x1c428f59, 26952, 522, 0x1c35c8f0},
    {0x1c242b58, 145888804, 2431597, 0x1c19ba10},
    {0x1d009b3a, 123928678, 1927132, 0x1d00ffff},
    {0x1c3d4a53, 391563105, 6525128, 0x1d00ffff},
    {0x1d00c9e7, 166993333, 2820927, 0x0905db7a},
    {0x1d00852e, -8521354, 697, 0x01010000},
    {0x1c15ce7b, 48153, 581, 0x1c293bfe},
    {0x1c2f729c, 503111399, 8386132, 0x1c031d69},
    {0x1d00fdd2, -8595331, 909, 0x01010000},
    {0x1d00ab3b, -5410314, 911, 0x01010000},
    {0x1d00f965, -40663, 9, 0x1c223971},
    {0x1c530fb5, 5585, 651, 0x1c10888f},
    {0x1b26d18f, -8631513, 294, 0x01010000},
    {0x1c745c4b, -27972, 328, 0x1c0bb4b5},
    {0x1c405f92, 45634, 562, 0x1c71e85c},
    {0x1d00dd21, 561362900, 9355772, 0x1d00ffff},
    {0x1d00949c, 445296417, 7421303, 0x1d00ffff},
    {0x1d009d72, 305169490, 5104189, 0x1311f43e},
    {0x1d00b150, 195571513, 3167048, 0x1d00ffff},
    {0x1d00894c, 305183302, 5086615, 0x1c4726a1},
    {0x1d00cc95, 130670639, 2178580, 0x1c185942},
    {0x1c072138, -3955817, 184, 0x01010000},
    {0x1d00c9b1, 359355085, 5988556, 0x1d00ffff},
    {0x1d00ec83, 1174197, 238, 0x1d00ffff},
    {0x1c44b60b, 215519908, 3592329, 0x1c1a5fce},
    {0x1c13ab78, 41402, 467, 0x1c255a71},
    {0x1c74af54, 43098, 57, 0x1d00ffff},
    {0x1c3f4d98, -215495, 834, 0x1a0ba1c7},
    {0x1d00f636, 362056788, 6043975, 0x1800ba7a},
    {0x1c7fce36, 360333277, 6006300, 0x1c0ece23},
    {0x1d0092be, -2619263, 227, 0x06014732},
    {0x1c65ba3c, 36786203, 613846, 0x1c0be0fe},
    {0x1c3260f7, 14076380, 362674, 0x01010000},
    {0x1d00e154, 14928, 334, 0x1d00afab},
    {0x1d009493, 49145, 104, 0x1d00ffff},
    {0x1c7ab082, 87397838, 1423237, 0x1d00ffff},
    {0x1b28a84d, 371544861, 6193070, 0x1b061a46},
    {0x1d00b500, 6324497, 811, 0x1d00ffff},
    {0x1c15c3c2, 199657252, 3239020, 0x1d00ffff},
    {0x1c57b416, 76183542, 1269271, 0x1d00ffff},
    {0x1c0798ed, 6398851, 750, 0x1d00ffff},
    {0x1d00ab02, 74101603, 1315101, 0x01010000},
    {0x1c4aae37, 100641, 979, 0x1d00ffff},
    {0x1d00d960, -4973348, 64, 0x01010000},
    {0x1d0081e7, 57477025, 957798, 0x1d00c927},
    {0x1c6174ab, -5030397, 261, 0x01010000},
    {0x1c3e47a5, -6785663, 248, 0x01010000},
    {0x1c59591e, -106361, 569, 0x1b1a5c0d},
    {0x1d00b905, 40607, 81, 0x1d00ffff},
    {0x1d009c05, -2641172, 130, 0x0600a05b},
    {0x1d00c12b, 187826183, 3122348, 0x1d00ffff},
    {0x1c41ce35, 52956, 252, 0x1d00ffff},
    {0x1c601a8d, 685719, 869, 0x1d00ffff},
    {0x1c2c2d8f, 477098505, 7951865, 0x1c171e8d},
    {0x1d00e9cf, 33433, 810, 0x1c705a1c},
    {0x1d008301, 5575, 389, 0x1c378cf5},
    {0x1c3b4913, 15836521, 208426, 0x1d00ffff},
    {0x1d00981a, 29429, 441, 0x1d00aef9},
    {0x1d008521, 33631, 146, 0x1d00ffff},
    {0x1c04f4b6, 2041265, 857, 0x1d00ffff},
    {0x1d0084ab, 571431428, 9523982, 0x1c5c3a3b},
    {0x1d00a5e0, 4258084, 428, 0x1d00ffff},
    {0x1d00c764, 28621, 649, 0x1c78fc87},
    {0x1c11ea67, 5249990, 35, 0x1d00ffff},
    {0x1c0e494c, 335125902, 5550008, 0x1d00ffff},
    {0x1c554cc6, 155797779, 2597542, 0x1c0619c3},
    {0x1c731b6f, 452570378, 7422657, 0x1d00ffff},
    {0x1c06fb00, 567106965, 9451345, 0x1c18a540},
    {0x1c34c388, 73895, 641, 0x1d00ffff},
    {0x1d00ba6f, 308570925, 5143456, 0x1c202dc2},
    {0x1c0d89d5, 160463307, 2674987, 0x1c02656d},
    {0x1c25997d, -10964, 432, 0x1c0659d9},
    {0x1c75af49, 517704032, 8736863, 0x01010000},
    {0x1d00b82a, 567782019, 9402880, 0x1d00ffff},
    {0x1d00b198, 84125, 995, 0x1d00ffff},
    {0x1d00a1ad, 19636, 158, 0x1d00ffff},
    {0x1d00fdbc, 98504, 993, 0x1d00ffff},
    {0x1d00f750, 87452, 662, 0x1d00ffff},
    {0x1c41462f, 600501932, 9865296, 0x1d00ffff},
    {0x1c54a067, -8200846, 862, 0x01010000},
    {0x1d00a8cd, 120716976, 2012152, 0x1c5dd22c},
    {0x1d00da55, 504856159, 8288656, 0x1d00ffff},
    {0x1c0b2aea, -5169774, 941, 0x01010000},
    {0x1c0bc7dc, 556969205, 9449408, 0x01010000},
    {0x1d00e53d, -988547, 227, 0x1404ba98},
    {0x1d00dbbf, -7522, 45, 0x1d0085f6},
    {0x1d00807a, 599953871, 9999714, 0x1c1fc49a},
    {0x1d009715, 70695566, 1178319, 0x1c7ed89a},
    {0x1d00f840, 131974531, 2054960, 0x1d00ffff},
    {0x1c6c164d, 511648427, 8688886, 0x01010000},
    {0x1c3762cd, 94295, 789, 0x1d00ffff},
    {0x1d0096fe, -2548254, 487, 0x0612ef3b},
    {0x1d009c3a, -15800, 226, 0x1c25e931},
    {0x1d00a16b, 243344196, 3896963, 0x1d00ffff},
    {0x1d00d8d0, -7596958, 299, 0x01010000},
    {0x1c39343e, 19024, 116, 0x1c65f4b2},
    {0x1d00e212, 503173, 775, 0x1d00ffff},
    {0x1d00f89e, 70010, 503, 0x1d00ffff},
    {0x1d00c444, 7709, 91, 0x1d00da18},
    {0x1d00ee6e, 560661007, 9344599, 0x1c73de24},
    {0x1d00e5f3, 13908, 92, 0x1d00ffff},
    {0x1c1198fc, 76620, 430, 0x1d00ca8e},
    {0x1d00e7d6, 368116291, 5976132, 0x1d00ffff},
    {0x1c1b2fdf, 42817, 11, 0x1d00ce44},
    {0x1d008f8b, 312518001, 5115678, 0x1d00ffff},
};

/** Build a chain of block indexes with given inter-block times, starting from the genesis bits. */
std::vector<CBlockIndex> BuildChain(const Consensus::Params& params, uint32_t genesis_bits, int64_t genesis_time,
                                    const std::vector<int64_t>& intervals)
{
    std::vector<CBlockIndex> blocks(intervals.size() + 1);
    blocks[0].pprev = nullptr;
    blocks[0].nHeight = 0;
    blocks[0].nTime = genesis_time;
    blocks[0].nBits = genesis_bits;
    for (size_t i = 1; i < blocks.size(); ++i) {
        blocks[i].pprev = &blocks[i - 1];
        blocks[i].nHeight = i;
        blocks[i].nTime = blocks[i - 1].nTime + intervals[i - 1];
        blocks[i].nBits = GetNextASERTWorkRequired(&blocks[i - 1], params);
    }
    return blocks;
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(pqch_asert_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(asert_mainnet_params)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    BOOST_CHECK(params.fPowUseASERT);
    BOOST_CHECK_EQUAL(params.nPowTargetSpacing, 60);
    BOOST_CHECK_EQUAL(params.nASERTHalfLife, 4 * 60 * 60);
    BOOST_CHECK_EQUAL(params.nASERTAnchorHeight, 0);
}

// Every vector was produced by an independent Python implementation of the specification.
BOOST_AUTO_TEST_CASE(asert_reference_vectors)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    for (const auto& v : ASERT_VECTORS) {
        arith_uint256 ref;
        ref.SetCompact(v.ref_bits);
        const uint32_t got = CalculateASERT(ref, params.nPowTargetSpacing, v.time_diff, v.height_diff, powLimit, params.nASERTHalfLife).GetCompact();
        BOOST_CHECK_MESSAGE(got == v.expected_bits,
                            strprintf("ref_bits=%08x time_diff=%d height_diff=%d: got %08x, expected %08x",
                                      v.ref_bits, v.time_diff, v.height_diff, got, v.expected_bits));
    }
}

// Blocks exactly on schedule keep the target unchanged.
BOOST_AUTO_TEST_CASE(asert_on_schedule)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    const uint32_t bits = 0x1b0404cb;
    const auto blocks = BuildChain(params, bits, 1'800'000'000, std::vector<int64_t>(5000, params.nPowTargetSpacing));
    for (const auto& b : blocks) BOOST_CHECK_EQUAL(b.nBits, bits);
}

// Falling one half-life behind schedule doubles the target; one half-life ahead halves it.
BOOST_AUTO_TEST_CASE(asert_half_life)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    const uint32_t bits = 0x1b0404cb;
    arith_uint256 ref;
    ref.SetCompact(bits);

    // One block that took 60 s + one half-life: the next target is exactly twice the reference.
    auto slow = BuildChain(params, bits, 1'800'000'000, {params.nPowTargetSpacing + params.nASERTHalfLife});
    arith_uint256 next;
    next.SetCompact(GetNextASERTWorkRequired(&slow.back(), params));
    BOOST_CHECK_EQUAL(next.GetCompact(), (ref * 2).GetCompact());

    // 241 blocks in the time of one block: one half-life ahead of schedule, target halves.
    std::vector<int64_t> fast(240, 0);
    fast.push_back(params.nPowTargetSpacing);
    auto quick = BuildChain(params, bits, 1'800'000'000, fast);
    next.SetCompact(GetNextASERTWorkRequired(&quick.back(), params));
    BOOST_CHECK_EQUAL(next.GetCompact(), (ref / 2).GetCompact());
}

// A long stall drives the target to powLimit, never above it; a burst never reaches zero.
BOOST_AUTO_TEST_CASE(asert_clamps)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);

    auto stalled = BuildChain(params, 0x1b0404cb, 1'800'000'000, {30 * 24 * 60 * 60});
    BOOST_CHECK_EQUAL(GetNextASERTWorkRequired(&stalled.back(), params), powLimit.GetCompact());

    auto burst = BuildChain(params, 0x03000001, 1'800'000'000, std::vector<int64_t>(20000, 0));
    arith_uint256 t;
    t.SetCompact(GetNextASERTWorkRequired(&burst.back(), params));
    BOOST_CHECK(t >= arith_uint256(1));
}

// Absolute scheduling: the result depends only on total time and height, not on the path taken.
BOOST_AUTO_TEST_CASE(asert_path_independence)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    auto a = BuildChain(params, 0x1b0404cb, 1'800'000'000, {30, 90, 60, 300, 0, 120});
    auto b = BuildChain(params, 0x1b0404cb, 1'800'000'000, {300, 0, 120, 30, 90, 60});
    BOOST_CHECK_EQUAL(GetNextASERTWorkRequired(&a.back(), params), GetNextASERTWorkRequired(&b.back(), params));
}

// The mainnet entry point dispatches to ASERT.
BOOST_AUTO_TEST_CASE(asert_get_next_work_required)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    auto chain = BuildChain(params, 0x1b0404cb, 1'800'000'000, {600, 30, 45});
    CBlockHeader header;
    header.nTime = chain.back().nTime + 60;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&chain.back(), &header, params), GetNextASERTWorkRequired(&chain.back(), params));
}

BOOST_AUTO_TEST_SUITE_END()
