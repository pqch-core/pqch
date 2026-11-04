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
    {0x1e7fffff, 60, 0, 0x1e7fffff},
    {0x1e7fffff, 14460, 0, 0x1e7fffff},
    {0x1e7fffff, -14340, 0, 0x1e3fffff},
    {0x1b0404cb, 14460, 0, 0x1b080996},
    {0x1b0404cb, -43140, 0, 0x1b008099},
    {0x1e7fffff, 120, 1, 0x1e7fffff},
    {0x1e7fffff, 14520, 1, 0x1e7fffff},
    {0x1e7fffff, -14280, 1, 0x1e3fffff},
    {0x1b0404cb, 14520, 1, 0x1b080996},
    {0x1b0404cb, -43080, 1, 0x1b008099},
    {0x1e7fffff, 180, 2, 0x1e7fffff},
    {0x1e7fffff, 14580, 2, 0x1e7fffff},
    {0x1e7fffff, -14220, 2, 0x1e3fffff},
    {0x1b0404cb, 14580, 2, 0x1b080996},
    {0x1b0404cb, -43020, 2, 0x1b008099},
    {0x1e7fffff, 14400, 239, 0x1e7fffff},
    {0x1e7fffff, 28800, 239, 0x1e7fffff},
    {0x1e7fffff, 0, 239, 0x1e3fffff},
    {0x1b0404cb, 28800, 239, 0x1b080996},
    {0x1b0404cb, -28800, 239, 0x1b008099},
    {0x1e7fffff, 14460, 240, 0x1e7fffff},
    {0x1e7fffff, 28860, 240, 0x1e7fffff},
    {0x1e7fffff, 60, 240, 0x1e3fffff},
    {0x1b0404cb, 28860, 240, 0x1b080996},
    {0x1b0404cb, -28740, 240, 0x1b008099},
    {0x1e7fffff, 14520, 241, 0x1e7fffff},
    {0x1e7fffff, 28920, 241, 0x1e7fffff},
    {0x1e7fffff, 120, 241, 0x1e3fffff},
    {0x1b0404cb, 28920, 241, 0x1b080996},
    {0x1b0404cb, -28680, 241, 0x1b008099},
    {0x1e7fffff, 86460, 1440, 0x1e7fffff},
    {0x1e7fffff, 100860, 1440, 0x1e7fffff},
    {0x1e7fffff, 72060, 1440, 0x1e3fffff},
    {0x1b0404cb, 100860, 1440, 0x1b080996},
    {0x1b0404cb, 43260, 1440, 0x1b008099},
    {0x1e7fffff, 31557660, 525960, 0x1e7fffff},
    {0x1e7fffff, 31572060, 525960, 0x1e7fffff},
    {0x1e7fffff, 31543260, 525960, 0x1e3fffff},
    {0x1b0404cb, 31572060, 525960, 0x1b080996},
    {0x1b0404cb, 31514460, 525960, 0x1b008099},
    {0x1e7fffff, 141360060, 2356000, 0x1e7fffff},
    {0x1e7fffff, 141374460, 2356000, 0x1e7fffff},
    {0x1e7fffff, 141345660, 2356000, 0x1e3fffff},
    {0x1b0404cb, 141374460, 2356000, 0x1b080996},
    {0x1b0404cb, 141316860, 2356000, 0x1b008099},
    {0x1b0404cb, -1000000000, 1000000, 0x01010000},
    {0x1b0404cb, 1000000000, 0, 0x1e7fffff},
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
    {0x1e029697, 41553, 233, 0x1e09bb62},
    {0x1e7fe3de, 161334765, 2688330, 0x1e7fffff},
    {0x1e6290ff, 329329560, 5361411, 0x1e7fffff},
    {0x1e3545bd, 67542, 765, 0x1e7fffff},
    {0x1e42367e, 517651412, 8581146, 0x1e7fffff},
    {0x1e428bfc, 9953742, 208087, 0x084c61d8},
    {0x1e0d670a, 108382389, 1806384, 0x1e0cf3dc},
    {0x1e081f5f, 79940347, 1332618, 0x1e039ead},
    {0x1e5780fa, 222283612, 3705487, 0x1e09b6be},
    {0x1e50d190, 575672954, 9701240, 0x01010000},
    {0x1e1e3ca6, 587958, 310, 0x1e7fffff},
    {0x1e1551b3, 6265, 1, 0x1e1ca788},
    {0x1e117302, 8421278, 270, 0x1e7fffff},
    {0x1e0c4bb5, 399471185, 6662127, 0x1c03806f},
    {0x1e18e95b, 264096310, 4401801, 0x1e0e1c5e},
    {0x1e0293cd, 1392397, 194, 0x1e7fffff},
    {0x1e7a02e1, 4776980, 74, 0x1e7fffff},
    {0x1e334d3d, -19268, 198, 0x1e0b6c4c},
    {0x1e7c497f, 93173, 898, 0x1e7fffff},
    {0x1e796263, 10411169, 174100, 0x1e16a234},
    {0x1e49965f, -33985, 268, 0x1e069727},
    {0x1e720857, -3611, 406, 0x1e1d95ec},
    {0x1e512e0b, 321937282, 5199924, 0x1e7fffff},
    {0x1e0463e3, -40916, 267, 0x1d4851f2},
    {0x1e6f4dd4, 39204, 420, 0x1e7fffff},
    {0x1e654318, -3515817, 756, 0x01010000},
    {0x1e4157fd, 239057225, 3983606, 0x1e7fffff},
    {0x1e211c3f, 540803216, 9022646, 0x19585ee5},
    {0x1e4f01b0, 140870146, 2347423, 0x1e7fffff},
    {0x1e54481f, 579144255, 9678443, 0x1103ba28},
    {0x1e77ee01, -12304, 464, 0x1e11516a},
    {0x1e22aad9, 449147846, 7486284, 0x1e087adb},
    {0x1e6b4aef, 324799111, 5458080, 0x0712d46b},
    {0x1e6f724b, 39165770, 783777, 0x01010000},
    {0x1e50cb18, 504518690, 8409019, 0x1e1b56a4},
    {0x1e6c60f6, 586174021, 9872908, 0x01010000},
    {0x1e0e8597, 48491, 80, 0x1e769d9c},
    {0x1e18bcbd, 46803, 814, 0x1e165d6b},
    {0x1e33c66a, 141015129, 2192914, 0x1e7fffff},
    {0x1e70ea50, 2240409, 131, 0x1e7fffff},
    {0x1e56889b, 23462446, 390774, 0x1e7fffff},
    {0x1e550017, -17215, 245, 0x1e123d7f},
    {0x1e6b109f, 95825880, 1597189, 0x1e521489},
    {0x1e14f6a4, 7603675, 370, 0x1e7fffff},
    {0x1e5967e8, 17488, 306, 0x1e557dc1},
    {0x1e74818d, 390446537, 6626773, 0x01010000},
    {0x1e21c5c9, 205062021, 3418504, 0x1e034e51},
    {0x1e01c2de, 452634959, 7543492, 0x1e05f9c0},
    {0x1e461491, -743375, 99, 0x180442c0},
    {0x1e0eb828, 93906, 616, 0x1e7fffff},
    {0x1e0b432d, 52347632, 980857, 0x01010000},
    {0x1e7dbe69, 35297, 285, 0x1e7fffff},
    {0x1e3b51c2, 538280617, 8971060, 0x1e7fffff},
    {0x1e5ade41, 42917, 302, 0x1e7fffff},
    {0x1e79f5df, -9053402, 561, 0x01010000},
    {0x1e519b50, 368088783, 6134049, 0x1e7fffff},
    {0x1e24896d, 132176515, 2092653, 0x1e7fffff},
    {0x1e758366, 1270, 426, 0x1e246549},
    {0x1e6af1e5, 9908881, 131, 0x1e7fffff},
    {0x1e584dfe, 8889710, 686, 0x1e7fffff},
    {0x1e0e0290, 546482567, 9108297, 0x1e06b452},
    {0x1e12d58a, 279347174, 4677533, 0x13031299},
    {0x1e72da7e, 429086639, 7146513, 0x1e7fffff},
    {0x1e24ac65, 225399065, 3756813, 0x1e16e885},
    {0x1e29b5d1, 163505698, 2580176, 0x1e7fffff},
    {0x1e0738ab, -9044, 61, 0x1e03e82f},
    {0x1e7f0761, 25925, 64, 0x1e7fffff},
    {0x1e4416a9, 89715, 653, 0x1e7fffff},
    {0x1e105fdb, 64087, 621, 0x1e3b65f7},
    {0x1e268995, 471195820, 7852472, 0x1e7fffff},
    {0x1e2ee4c3, 69821, 257, 0x1e7fffff},
    {0x1e3b4a87, 87738, 808, 0x1e7fffff},
    {0x1e59d6d2, -4116, 223, 0x1e269795},
    {0x1e1eb438, 7783964, 516, 0x1e7fffff},
    {0x1e7b7078, 159817329, 2664230, 0x1e1544d4},
    {0x1e253f08, 969, 103, 0x1e1ce63f},
    {0x1e1dc258, 59548995, 1013773, 0x13122c8a},
    {0x1e55d69b, 16368, 648, 0x1e1cf587},
    {0x1e3186bb, 307, 433, 0x1e0e5a2e},
    {0x1e20ff8b, 256750971, 4384751, 0x01010000},
    {0x1e1816b6, -4679457, 949, 0x01010000},
    {0x1e7e7d0b, 21777, 483, 0x1e5929ba},
    {0x1e19a8d6, 419974658, 6999421, 0x1e283895},
    {0x1e467c60, -447650, 153, 0x1a551539},
    {0x1e06c0a2, 4883673, 0, 0x1e7fffff},
    {0x1e50fda0, 33208, 615, 0x1e439ceb},
    {0x1e0b99be, 279492952, 4658256, 0x1e0a4d61},
    {0x1e5d3729, -8808573, 603, 0x01010000},
    {0x1e6c17b2, 50528, 576, 0x1e7fffff},
    {0x1e7dfea4, 83121, 454, 0x1e7fffff},
    {0x1e720c44, 12559517, 208605, 0x1e7fffff},
    {0x1e5ae904, 243144863, 4179323, 0x01010000},
    {0x1e3a698e, 6631219, 756, 0x1e7fffff},
    {0x1e233f59, -8362924, 76, 0x01010000},
    {0x1e6e69df, 40670, 986, 0x1e2d3656},
    {0x1e0ea8e0, -25673, 495, 0x1e01045e},
    {0x1e50f1d2, -2346436, 115, 0x0a078228},
    {0x1e4efd66, 429067494, 7151136, 0x1e4c4862},
    {0x1e3045d4, 302021231, 5022474, 0x1e7fffff},
    {0x1e64be33, 37413, 699, 0x1e50c93b},
    {0x1e1ed833, 552173295, 9203716, 0x1e02d0ee},
    {0x1e41466d, 185041975, 2922955, 0x1e7fffff},
    {0x1e6ce7bd, 569298, 550, 0x1e7fffff},
    {0x1e57442f, 54959, 948, 0x1e4f56ba},
    {0x1e193b38, 151112111, 2619402, 0x01010000},
    {0x1e46fc01, 37778493, 510714, 0x1e7fffff},
    {0x1e2e8ab2, 590099706, 9835633, 0x1e075a59},
    {0x1e26cffc, 9118123, 912, 0x1e7fffff},
    {0x1e49555e, 570401958, 9473882, 0x1e7fffff},
    {0x1e601783, 338481610, 5726902, 0x01010000},
    {0x1e55e09b, 408909993, 6975548, 0x01010000},
    {0x1e6686f2, -7868655, 926, 0x01010000},
    {0x1e692870, 12382, 681, 0x1e1a9fdb},
    {0x1e5a1bde, 6244082, 486, 0x1e7fffff},
    {0x1e00ad8d, 321535664, 5358958, 0x1e009e95},
    {0x1e6ccc0e, 51866, 831, 0x1e777e41},
    {0x1e6a3122, -3858545, 572, 0x01010000},
    {0x1e2a1f4b, -5790938, 169, 0x01010000},
    {0x1e7bd67f, 231243743, 3999335, 0x01010000},
    {0x1e0de959, 144982496, 2417291, 0x1e00fbfd},
    {0x1e5d1ae2, -4216, 120, 0x1e359788},
    {0x1e52c987, -5073830, 690, 0x01010000},
    {0x1e6cc8ee, 191485665, 3190689, 0x1e7fffff},
    {0x1e3025da, 3610087, 356, 0x1e7fffff},
    {0x1e52a339, 129572074, 2175373, 0x1614b185},
    {0x1e12421a, 203313249, 3388308, 0x1e2510b4},
    {0x1e10e4f2, -4013212, 251, 0x01010000},
    {0x1e577f65, 108370155, 1857374, 0x0402226c},
    {0x1e61c04f, 4812518, 99, 0x1e7fffff},
    {0x1e329a4f, 171175618, 3006461, 0x01010000},
    {0x1e5d8fb8, 232661111, 3811586, 0x1e7fffff},
    {0x1e5f3e47, 6749238, 164, 0x1e7fffff},
    {0x1e211041, 485771659, 8171756, 0x01010000},
    {0x1e6b07de, 30337830, 350796, 0x1e7fffff},
    {0x1e0de290, -4851654, 536, 0x01010000},
    {0x1e0e9af0, 285668311, 4880295, 0x01010000},
    {0x1e271dda, -2392641, 265, 0x0941282d},
    {0x1e01937d, 247258706, 4121388, 0x1d7b4532},
    {0x1e56345c, 89508, 972, 0x1e7fffff},
    {0x1e29661b, 476768196, 7969357, 0x12188474},
    {0x1e02ad36, 376565157, 6196112, 0x1e7fffff},
    {0x1e6e4d68, 125758905, 2004752, 0x1e7fffff},
    {0x1e688c4f, 127877634, 2131386, 0x1e4fe5a5},
    {0x1e6e902e, 300472514, 5008366, 0x1e1ab820},
    {0x1e43788e, 7941443, 63, 0x1e7fffff},
    {0x1e009a71, 302110393, 5170814, 0x01010000},
    {0x1e6c4368, 102418536, 1706973, 0x1e6cc3f8},
    {0x1e21f027, 14276649, 274891, 0x0b08c9ca},
    {0x1e6befcc, 57181009, 1014242, 0x01010000},
    {0x1e6afb9a, 568560706, 9393302, 0x1e7fffff},
    {0x1e1c7edf, 49562, 62, 0x1e7fffff},
    {0x1e4167b4, 18095564, 179129, 0x1e7fffff},
    {0x1e2b6a44, 5647552, 338, 0x1e7fffff},
    {0x1e68a261, 440163094, 7336253, 0x1e3a5198},
    {0x1e456dd8, 2820401, 404, 0x1e7fffff},
    {0x1e43b1e1, 388802319, 6508928, 0x0f34255e},
    {0x1e27010e, 108623963, 1810941, 0x1e08236c},
    {0x1e712fde, -7982, 58, 0x1e410205},
    {0x1e4adf0f, 37518, 405, 0x1e7fffff},
    {0x1e5b3a44, 13382, 718, 0x1e15c7f9},
    {0x1e315691, 469153988, 7818395, 0x1e7fffff},
    {0x1e5533f7, 187925315, 3185431, 0x03011bca},
    {0x1e661731, -32012, 207, 0x1e0bfe38},
    {0x1e594d4d, 9120187, 514, 0x1e7fffff},
    {0x1e735d9c, 105488702, 1658253, 0x1e7fffff},
    {0x1e5957ec, 4658341, 519, 0x1e7fffff},
    {0x1e1cb809, -3625479, 48891, 0x01010000},
    {0x1e5841df, 115256, 978, 0x1e7fffff},
    {0x1e04dc13, 75384, 600, 0x1e204300},
    {0x1e78cc25, 541784262, 9085281, 0x0200b200},
    {0x1e48f00d, -3242128, 304, 0x020da400},
    {0x1e10d0c4, 299994967, 5097056, 0x01010000},
    {0x1e0cfe85, -8353, 11, 0x1e08654c},
    {0x1e46c0d7, 388020719, 6581888, 0x01010000},
    {0x1e7881b5, 9113517, 229, 0x1e7fffff},
    {0x1e4c8bc7, 73147, 793, 0x1e7fffff},
    {0x1e6ac525, -32084, 312, 0x1e093a64},
    {0x1e7eff6e, -35587, 82, 0x1e120577},
    {0x1e4f81df, 208662039, 3429167, 0x1e7fffff},
    {0x1e78d822, 235064598, 3931174, 0x177bc90c},
    {0x1e75d821, 382002772, 6427215, 0x01010000},
    {0x1e7053ae, 74324, 439, 0x1e7fffff},
    {0x1e0160ad, 220919199, 3681088, 0x1e126934},
    {0x1e19c2cd, 5608341, 93895, 0x1e07942b},
    {0x1e40bf51, 489081781, 8151465, 0x1e301636},
    {0x1e750838, -4283555, 502, 0x01010000},
    {0x1e09c96c, 24356, 558, 0x1e064a28},
    {0x1e4158fb, -1477547, 958, 0x110ac0d4},
    {0x1e7d9902, 35566388, 747026, 0x01010000},
    {0x1e3e858f, 159054787, 2650582, 0x1e7fffff},
    {0x1e586084, 46881, 417, 0x1e7fffff},
    {0x1e6b0e7a, 294057713, 4812935, 0x1e7fffff},
    {0x1e19a90c, 149213798, 2578666, 0x01010000},
    {0x1e393c5e, 409138005, 6818131, 0x1e7fffff},
    {0x1e6e026a, 388830964, 6479954, 0x1e7fffff},
    {0x1e63e7b5, 176489882, 2886454, 0x1e7fffff},
    {0x1e44e155, 343256285, 5643325, 0x1e7fffff},
    {0x1e4b16e7, 56609, 833, 0x1e670199},
    {0x1e7d057f, 475388861, 7802437, 0x1e7fffff},
    {0x1e62ac4e, 3931008, 482, 0x1e7fffff},
    {0x1e2d2f43, 315189600, 5252842, 0x1e70df35},
    {0x1e6ebe61, 6853, 371, 0x1e349ab6},
    {0x1e35f1db, 227893569, 3929957, 0x01010000},
    {0x1e59f2ad, 385984413, 6411908, 0x1e7fffff},
    {0x1e02cdc2, 43835418, 729979, 0x1e105686},
    {0x1e2549a0, 56675, 801, 0x1e384899},
    {0x1e3a6a88, 294304674, 4905355, 0x1e1a2aea},
    {0x1e57944c, -27370, 31, 0x1e1562bb},
    {0x1e2dda8b, -5252297, 775, 0x01010000},
    {0x1e73177a, 515195449, 8556722, 0x1e7fffff},
    {0x1e0a95ae, 8189103, 902, 0x1e7fffff},
    {0x1e6c5b7e, 233097877, 3885914, 0x1e06f6af},
    {0x1e3463db, 163431887, 2724131, 0x1e1837ba},
    {0x1e7d173a, 245069548, 4007194, 0x1e7fffff},
    {0x1e58ecf7, 42100, 142, 0x1e7fffff},
    {0x1e38edd9, 36348, 691, 0x1e2c61e9},
    {0x1e48cce8, 19043, 337, 0x1e449964},
    {0x1e6be0a9, 85336, 580, 0x1e7fffff},
    {0x1e562370, 392413171, 6595897, 0x01560000},
    {0x1e2e60e7, -13134, 244, 0x1e0c25ce},
    {0x1e0a5f2a, 22567, 854, 0x1e0299f0},
    {0x1e55561a, 36859, 488, 0x1e7a8a6f},
    {0x1e7ce894, 447312300, 7455891, 0x1e112d4a},
    {0x1e06fc4a, 8223257, 671, 0x1e7fffff},
    {0x1e717e48, 33012, 868, 0x1e2d3270},
    {0x1e0bbb1a, 438158906, 7218416, 0x1e7fffff},
    {0x1e68f1a5, -2485806, 216, 0x0902458f},
    {0x1e7ef88e, 395906190, 6707073, 0x01010000},
    {0x1e05b159, 4637, 854, 0x1e009a35},
    {0x1e2f9cb7, 82731, 940, 0x1e7fffff},
    {0x1e4ac128, 213379129, 3597201, 0x090eb28b},
    {0x1e3aa586, 623970, 945, 0x1e7fffff},
    {0x1e292e1c, 34509194, 574779, 0x1e790119},
    {0x1e600593, 352796019, 5783114, 0x1e7fffff},
    {0x1e0430e1, 27067497, 451447, 0x1e01a609},
    {0x1e66c051, 10287694, 171375, 0x1e7fffff},
    {0x1e09e440, 530733500, 8846056, 0x1e0257e5},
    {0x1e77af39, -9976714, 383, 0x01010000},
    {0x1e03d16e, 80829, 710, 0x1e17f9a1},
    {0x1e542342, 1620555, 9, 0x1e7fffff},
    {0x1e72d944, -21064, 428, 0x1e0c1209},
    {0x1e343bba, 474187321, 7902304, 0x1e7fffff},
    {0x1e395f6d, 110743769, 1721486, 0x1e7fffff},
    {0x1e1f2c5b, 70069, 756, 0x1e661cf9},
    {0x1e7ec80c, 511789289, 8530767, 0x1e083d42},
    {0x1e011ccd, 105307, 842, 0x1e0f809c},
    {0x1e5ef385, 442182508, 7369923, 0x1e32f539},
    {0x1e3ab7c7, 86736, 792, 0x1e7fffff},
    {0x1e6d4615, 4388069, 991, 0x1e7fffff},
    {0x1e259971, -625766, 388, 0x19011d3a},
    {0x1e0ae4ab, 450146436, 7505614, 0x1c4a7b6a},
    {0x1e00d734, 20686, 722, 0x1d482d6a},
    {0x1e5a36b4, 130944492, 2031254, 0x1e7fffff},
    {0x1e071cda, -16871, 136, 0x1e022034},
    {0x1e4e987d, 68443792, 1266573, 0x01010000},
    {0x1e62bd61, 335019145, 5584328, 0x1e0dfe1c},
    {0x1e654428, 40008325, 666384, 0x1e7fffff},
    {0x1e01cb30, 58701362, 977697, 0x1e0bffaf},
    {0x1e6dc0c7, -4700912, 204, 0x01010000},
    {0x1e25f48e, 108272, 890, 0x1e7fffff},
    {0x1e048784, 68507, 777, 0x1e0cf362},
    {0x1e19a33e, -9857394, 297, 0x01010000},
    {0x1e104ad1, 38957, 592, 0x1e132b36},
    {0x1e7ab657, 398955493, 6767423, 0x01010000},
    {0x1e7b60cb, 16345387, 272851, 0x1e23c0fd},
    {0x1e526fc8, 75810, 466, 0x1e7fffff},
    {0x1e205845, 463149163, 7730224, 0x1875dfee},
    {0x1e0add1e, 46867, 125, 0x1e480ffc},
    {0x1e6d4977, -7181174, 543, 0x01010000},
    {0x1e3f1e58, -9186, 759, 0x1e048479},
    {0x1e0afae9, 80916, 620, 0x1e59cb90},
    {0x1e6637df, 310304336, 5171788, 0x1e587721},
    {0x1e496485, -26729, 458, 0x1e056291},
    {0x1e07a448, 468693499, 7811695, 0x1e052254},
    {0x1e0d5938, 4280888, 271, 0x1e7fffff},
    {0x1e40d936, 259714459, 4328536, 0x1e483bf3},
    {0x1e49f8f3, -9854903, 122, 0x01010000},
    {0x1e176004, 1506313, 142, 0x1e7fffff},
    {0x1e7ecf29, 265336591, 4362073, 0x1e7fffff},
    {0x1e121525, 145888804, 2431597, 0x1e0cdca8},
    {0x1e64389b, 20967, 117, 0x1e7fffff},
    {0x1e31c893, 491916298, 8197749, 0x1e7fffff},
    {0x1e5a43c9, 518868193, 8685508, 0x0b029e5f},
    {0x1e573185, 48568832, 810346, 0x1e0723a7},
    {0x1e767b91, -8861271, 563, 0x01010000},
    {0x1e22937f, 3584135, 984, 0x1e7fffff},
    {0x1e2d7a0a, 1281954, 138, 0x1e7fffff},
    {0x1e6d7258, 33216, 660, 0x1e5040f9},
    {0x1e0c1e86, 326579844, 5443509, 0x1e02c205},
    {0x1e7a1832, 618928, 9937, 0x1e7fffff},
    {0x1e54034a, 812994, 797, 0x1e7fffff},
    {0x1e131598, 348513102, 5808310, 0x1e263f04},
    {0x1e265ee4, 185525180, 3091810, 0x1e54fe90},
    {0x1e0e8f94, 588546477, 9808804, 0x1e22edba},
    {0x1e13369d, 305169490, 5104189, 0x150230e3},
    {0x1e76d6b1, 517355113, 8530108, 0x1e7fffff},
    {0x1e578c89, 350158549, 5835492, 0x1e7fffff},
    {0x1e24c7cc, 48154, 132, 0x1e7fffff},
    {0x1e170b07, -3918497, 806, 0x01010000},
    {0x1e0d16c9, -11591, 365, 0x1e029a75},
    {0x1e6debd8, 1219497, 993, 0x1e7fffff},
    {0x1e63d744, 342575766, 5837177, 0x01010000},
    {0x1e4f8785, -3576531, 835, 0x01010000},
    {0x1e5ef03b, 31817, 259, 0x1e7fffff},
    {0x1e09ff99, 285223404, 4867202, 0x01010000},
    {0x1e73c75c, 247403012, 3957834, 0x1e7fffff},
    {0x1e0e487f, 14208, 366, 0x1e09ce45},
    {0x1e4efa39, 343869235, 5579092, 0x1e7fffff},
    {0x1e3fe8a7, -1235284, 983, 0x1311524c},
    {0x1e32b048, 235460280, 4059463, 0x01010000},
    {0x1e3a4be8, 38295, 167, 0x1e7fffff},
    {0x1e4ce814, 295013494, 5036116, 0x01010000},
    {0x1e2aa1fa, 20963, 502, 0x1e1b5aec},
    {0x1e49a126, -19188, 563, 0x1e05bc0a},
    {0x1e63780d, -78713, 637, 0x1d5b4137},
    {0x1e285f92, 76183542, 1269271, 0x1e7fffff},
    {0x1e5dd807, 6399271, 757, 0x1e7fffff},
    {0x1e605cab, 349526594, 5826064, 0x1e0ffedd},
    {0x1e0b1af4, 67221, 216, 0x1e7fffff},
    {0x1e0a829d, 150718057, 2511376, 0x1e39dc2a},
    {0x1e411f67, -4978772, 434, 0x01010000},
    {0x1e47013f, 30290, 151, 0x1e7fffff},
    {0x1e0c34c8, 264953998, 4516781, 0x01010000},
    {0x1e3beb8d, -18826, 244, 0x1e0beeab},
    {0x1e6e7443, -5669922, 950, 0x01010000},
    {0x1e752982, 1255414, 830, 0x1e7fffff},
    {0x1e7938e3, 438197417, 7158222, 0x1e7fffff},
    {0x1e529ccb, -7508613, 29, 0x01010000},
    {0x1e70820f, 519499765, 8598874, 0x1e7fffff},
    {0x1e2b1c6d, 25824, 468, 0x1e269145},
    {0x1e054a51, 29721, 294, 0x1e096fce},
    {0x1e63e9a5, -33555, 98, 0x1e0eed83},
    {0x1e156b52, 393185263, 6552686, 0x1e44249d},
    {0x1e076776, 447467172, 7394443, 0x1e7fffff},
    {0x1e058578, 4877, 998, 0x1d63d347},
    {0x1e57ea74, -156473, 852, 0x1d0106c4},
    {0x1e029852, 4264684, 538, 0x1e7fffff},
    {0x1e5125c7, 84862080, 1415062, 0x1e0ae777},
    {0x1e01265d, 457463415, 7623915, 0x1e048618},
    {0x1e6098bb, 335125902, 5550008, 0x1e7fffff},
    {0x1e755c55, 574831239, 9581433, 0x1e0864c4},
    {0x1e1c1173, 1696511, 453, 0x1e7fffff},
    {0x1e4d5394, 567106965, 9451345, 0x1e7fffff},
    {0x1e503a34, 82475, 784, 0x1e7fffff},
    {0x1e58efba, 36221, 313, 0x1e7fffff},
    {0x1e2aec0d, 160463307, 2674987, 0x1e0798cd},
    {0x1e361a25, 10816, 795, 0x1e092371},
    {0x1e506275, 506839652, 8555790, 0x01010000},
    {0x1e0c4bb8, 572560959, 9482529, 0x1e7fffff},
    {0x1e7c74b1, 75125, 845, 0x1e7fffff},
    {0x1e13c6a1, 57436, 788, 0x1e2026dd},
    {0x1e7c2dfd, 45224, 39, 0x1e7fffff},
    {0x1e041b18, 665900, 822, 0x1e7fffff},
    {0x1e39da7c, 77441, 567, 0x1e7fffff},
    {0x1e06aa0e, 146142074, 2435064, 0x1e29db87},
    {0x1e5cdfbb, 221635790, 3633551, 0x1e7fffff},
    {0x1e75ae0b, -5188794, 624, 0x01010000},
    {0x1e6aa00b, 556969205, 9449408, 0x01010000},
    {0x1e1c73e5, -949727, 874, 0x16009641},
    {0x1e05aedb, 49418, 994, 0x1e0376e8},
    {0x1e1f4280, 599953871, 9999714, 0x1e07bac0},
    {0x1e111c97, 489265830, 8153765, 0x1e74a0ab},
    {0x1e1695f8, 22916, 125, 0x1e2f4d86},
    {0x1e424ac1, 5594993, 904, 0x1e7fffff},
    {0x1e1900c5, 87155, 670, 0x1e7fffff},
    {0x1e3ce696, -2574714, 46, 0x0807a311},
    {0x1e1c429c, 6580, 546, 0x1e07fe05},
    {0x1e273aa1, 7012153, 237, 0x1e7fffff},
    {0x1e257dd8, -34244, 233, 0x1e03ab4d},
    {0x1e43f938, 5440474, 403, 0x1e7fffff},
    {0x1e597692, 174087231, 2738055, 0x1e7fffff},
    {0x1e3efdf8, 135392066, 2255637, 0x1e7fffff},
    {0x1e551254, 105388924, 1757076, 0x1e0f425b},
    {0x1e1bf6ee, 560661007, 9344599, 0x1e0d96f7},
    {0x1e0b95e5, 52128, 729, 0x1e114c34},
    {0x1e35d311, 71100, 338, 0x1e7fffff},
    {0x1e0757e7, 9570211, 364, 0x1e7fffff},
    {0x1e01711b, -832613, 594, 0x1612bebb},
    {0x1e13d27d, 321559932, 5360174, 0x1e01bce0},
    {0x1e0734ae, -46947, 54, 0x1e00a43d},
    {0x1e36e406, 29635728, 492998, 0x1e7fffff},
    {0x1e30d4fd, 362286066, 6050920, 0x180124bb},
    {0x1e4458a7, 5871, 710, 0x1e0ba180},
    {0x1e090c61, 573484826, 9527531, 0x1e7fffff},
    {0x1e59bacb, -36865, 64, 0x1e0c9cb0},
    {0x1e3b6561, -6957791, 91, 0x01010000},
    {0x1e79f126, 277163245, 4466122, 0x1e7fffff},
    {0x1e660559, 151448415, 2524667, 0x1e1638cf},
    {0x1e045b72, 61945, 814, 0x1e082a3c},
    {0x1e77fba7, 46020, 261, 0x1e7fffff},
    {0x1e66c999, 456721980, 7611520, 0x1e7fffff},
    {0x1e5e89c4, -4127653, 618, 0x01010000},
    {0x1e0f8e47, 315562172, 5155763, 0x1e7fffff},
    {0x1e4f59ab, -563473, 188, 0x1954066e},
    {0x1e06abe2, -5422246, 344, 0x01010000},
    {0x1e080ce0, -12794, 723, 0x1e008993},
    {0x1e2d1395, 9101372, 304, 0x1e7fffff},
    {0x1e687d55, -9331439, 105, 0x01010000},
    {0x1e094868, 456928994, 7616345, 0x1e00c4aa},
    {0x1e6e82b6, 34548, 294, 0x1e7fffff},
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
    arith_uint256 doubled{ref};
    doubled *= 2;
    BOOST_CHECK_EQUAL(next.GetCompact(), doubled.GetCompact());

    // 241 blocks in the time of one block: one half-life ahead of schedule, target halves.
    std::vector<int64_t> fast(240, 0);
    fast.push_back(params.nPowTargetSpacing);
    auto quick = BuildChain(params, bits, 1'800'000'000, fast);
    next.SetCompact(GetNextASERTWorkRequired(&quick.back(), params));
    arith_uint256 halved{ref};
    halved /= 2;
    BOOST_CHECK_EQUAL(next.GetCompact(), halved.GetCompact());
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
