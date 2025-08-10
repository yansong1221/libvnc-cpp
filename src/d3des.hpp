/////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2002-2024 UltraVNC Team Members. All Rights Reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//
//  If the source code for the program is not available from the place from
//  which you received this file, check
//  https://uvnc.com/
//
////////////////////////////////////////////////////////////////////////////

/*
 * This is D3DES (V5.09) by Richard Outerbridge with the double and
 * triple-length support removed for use in VNC. Also the bytebit[] array
 * has been reversed so that the most significant bit in each byte of the
 * key is ignored, not the least significant.
 *
 * These changes are
 * Copyright (C) 1999 AT&T Laboratories Cambridge. All Rights Reserved.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <cstdint>
#include <cstring>

#define EN0 0
#define DE1 1

namespace libvnc {

namespace {

static uint32_t KnL[32] = { 0 };

static const uint16_t bytebit[8] = {
    01, 02, 04, 010, 020, 040, 0100, 0200
};

static const uint32_t bigbyte[24] = {
    0x800000, 0x400000, 0x200000, 0x100000,
    0x80000,  0x40000,  0x20000,  0x10000,
    0x8000,   0x4000,   0x2000,   0x1000,
    0x800,    0x400,    0x200,    0x100,
    0x80,     0x40,     0x20,     0x10,
    0x8,      0x4,      0x2,      0x1
};

static const uint8_t pc1[56] = {
    56, 48, 40, 32, 24, 16,  8,      0, 57, 49, 41, 33, 25, 17,
     9,  1, 58, 50, 42, 34, 26,     18, 10,  2, 59, 51, 43, 35,
    62, 54, 46, 38, 30, 22, 14,      6, 61, 53, 45, 37, 29, 21,
    13,  5, 60, 52, 44, 36, 28,     20, 12,  4, 27, 19, 11,  3
};

static const uint8_t totrot[16] = {
    1,2,4,6,8,10,12,14,15,17,19,21,23,25,27,28
};

static const uint8_t pc2[48] = {
    13, 16, 10, 23,  0,  4,  2, 27, 14,  5, 20,  9,
    22, 18, 11,  3, 25,  7, 15,  6, 26, 19, 12,  1,
    40, 51, 30, 36, 46, 54, 29, 39, 50, 44, 32, 47,
    43, 48, 38, 55, 33, 52, 45, 41, 49, 35, 28, 31
};

static const uint32_t SP1[64] = {
    0x01010400,0x00000000,0x00010000,0x01010404,0x01010004,0x00010404,0x00000004,0x00010000,
    0x00000400,0x01010400,0x01010404,0x00000400,0x01000404,0x01010004,0x01000000,0x00000004,
    0x00000404,0x01000400,0x01000400,0x00010400,0x00010400,0x01010000,0x01010000,0x01000404,
    0x00010004,0x01000004,0x01000004,0x00010004,0x00000000,0x00000404,0x00010404,0x01000000,
    0x00010000,0x01010404,0x00000004,0x01010000,0x01010400,0x01000000,0x01000000,0x00000400,
    0x01010004,0x00010000,0x00010400,0x01000004,0x00000400,0x00000004,0x01000404,0x00010404,
    0x01010404,0x00010004,0x01010000,0x01000404,0x01000004,0x00000404,0x00010404,0x01010400,
    0x00000404,0x01000400,0x01000400,0x00000000,0x00010004,0x00010400,0x00000000,0x01010004
};

static const uint32_t SP2[64] = {
    0x80108020,0x80008000,0x00008000,0x00108020,0x00100000,0x00000020,0x80100020,0x80008020,
    0x80000020,0x80108020,0x80108000,0x80000000,0x80008000,0x00100000,0x00000020,0x80100020,
    0x00108000,0x00100020,0x80008020,0x00000000,0x80000000,0x00008000,0x00108020,0x80100000,
    0x00100020,0x80000020,0x00000000,0x00108000,0x00008020,0x80108000,0x80100000,0x00008020,
    0x00000000,0x00108020,0x80100020,0x00100000,0x80008020,0x80100000,0x80108000,0x00008000,
    0x80100000,0x80008000,0x00000020,0x80108020,0x00108020,0x00000020,0x00008000,0x80000000,
    0x00008020,0x80108000,0x00100000,0x80000020,0x00100020,0x80008020,0x80000020,0x00100020,
    0x00108000,0x00000000,0x80008000,0x00008020,0x80000000,0x80100020,0x80108020,0x00108000
};

static const uint32_t SP3[64] = {
    0x00000208,0x08020200,0x00000000,0x08020008,0x08000200,0x00000000,0x00020208,0x08000200,
    0x00020008,0x08000008,0x08000008,0x00020000,0x08020208,0x00020008,0x08020000,0x00000208,
    0x08000000,0x00000008,0x08020200,0x00000200,0x00020200,0x08020000,0x08020008,0x00020208,
    0x08000208,0x00020200,0x00020000,0x08000208,0x00000008,0x08020208,0x00000200,0x08000000,
    0x08020200,0x08000000,0x00020008,0x00000208,0x00020000,0x08020200,0x08000200,0x00000000,
    0x00000200,0x00020008,0x08020208,0x08000200,0x08000008,0x00000200,0x00000000,0x08020008,
    0x08000208,0x00020000,0x08000000,0x08020208,0x00000008,0x00020208,0x00020200,0x08000008,
    0x08020000,0x08000208,0x00000208,0x08020000,0x00020208,0x00000008,0x08020008,0x00020200
};

static const uint32_t SP4[64] = {
    0x00802001,0x00002081,0x00002081,0x00000080,0x00802080,0x00800081,0x00800001,0x00002001,
    0x00000000,0x00802000,0x00802000,0x00802081,0x00000081,0x00000000,0x00800080,0x00800001,
    0x00000001,0x00002000,0x00800000,0x00802001,0x00000080,0x00800000,0x00002001,0x00002080,
    0x00800081,0x00000001,0x00002080,0x00800080,0x00002000,0x00802080,0x00802081,0x00000081,
    0x00800080,0x00800001,0x00802000,0x00802081,0x00000081,0x00000000,0x00000000,0x00802000,
    0x00002080,0x00800080,0x00800081,0x00000001,0x00802001,0x00002081,0x00002081,0x00000080,
    0x00802081,0x00000081,0x00000001,0x00002000,0x00800001,0x00002001,0x00802080,0x00800081,
    0x00002001,0x00002080,0x00800000,0x00802001,0x00000080,0x00800000,0x00002000,0x00802080
};

static const uint32_t SP5[64] = {
    0x00000100,0x02080100,0x02080000,0x42000100,0x00080000,0x00000100,0x40000000,0x02080000,
    0x40080100,0x00080000,0x02000100,0x40080100,0x42000100,0x42080000,0x00080100,0x40000000,
    0x02000000,0x40080000,0x40080000,0x00000000,0x40000100,0x42080100,0x42080100,0x02000100,
    0x42080000,0x40000100,0x00000000,0x42000000,0x02080100,0x02000000,0x42000000,0x00080100,
    0x00080000,0x42000100,0x00000100,0x02000000,0x40000000,0x02080000,0x42000100,0x40080100,
    0x02000100,0x40000000,0x42080000,0x02080100,0x40080100,0x00000100,0x02000000,0x42080000,
    0x42080100,0x00080100,0x42000000,0x42080100,0x02080000,0x00000000,0x40080000,0x42000000,
    0x00080100,0x02000100,0x40000100,0x00080000,0x00000000,0x40080000,0x02080100,0x40000100
};

static const uint32_t SP6[64] = {
    0x20000010,0x20400000,0x00004000,0x20404010,0x20400000,0x00000010,0x20404010,0x00400000,
    0x20004000,0x00404010,0x00400000,0x20000010,0x00400010,0x20004000,0x20000000,0x00004010,
    0x00000000,0x00400010,0x20004010,0x00004000,0x00404000,0x20004010,0x00000010,0x20400010,
    0x20400010,0x00000000,0x00404010,0x20404000,0x00004010,0x00404000,0x20404000,0x20000000,
    0x20004000,0x00000010,0x20400010,0x00404000,0x20404010,0x00400000,0x00004010,0x20000010,
    0x00400000,0x20004000,0x20000000,0x00004010,0x20000010,0x20404010,0x00404000,0x20400000,
    0x00404010,0x20404000,0x00000000,0x20400010,0x00000010,0x00004000,0x20400000,0x00404010,
    0x00004000,0x00400010,0x20004010,0x00000000,0x20404000,0x20000000,0x00400010,0x20004010
};

static const uint32_t SP7[64] = {
    0x00200000,0x04200002,0x04000802,0x00000000,0x00000800,0x04000802,0x00200802,0x04200800,
    0x04200802,0x00200000,0x00000000,0x04000002,0x00000002,0x04000000,0x04200002,0x00000802,
    0x04000800,0x00200802,0x00200002,0x04000800,0x04000002,0x04200000,0x04200800,0x00200002,
    0x04200000,0x00000800,0x00000802,0x04200802,0x00200800,0x00000002,0x04000000,0x00200800,
    0x04000000,0x00200800,0x00200000,0x04000802,0x04000802,0x04200002,0x04200002,0x00000002,
    0x00200002,0x04000000,0x04000800,0x00200000,0x04200800,0x00000802,0x00200802,0x04200800,
    0x00000802,0x04000002,0x04200802,0x04200000,0x00200800,0x00000000,0x00000002,0x04200802,
    0x00000000,0x00200802,0x04200000,0x00000800,0x04000002,0x04000800,0x00000800,0x00200002
};

static const uint32_t SP8[64] = {
    0x10001040,0x00001000,0x00040000,0x10041040,0x10000000,0x10001040,0x00000040,0x10000000,
    0x00040040,0x10040000,0x10041040,0x00041000,0x10041000,0x00041040,0x00001000,0x00000040,
    0x10040000,0x10000040,0x10001000,0x00001040,0x00041000,0x00040040,0x10040040,0x10041000,
    0x00001040,0x00000000,0x00000000,0x10040040,0x10000040,0x10001000,0x00041040,0x00040000,
    0x00041040,0x00040000,0x10041000,0x00001000,0x00000040,0x10040040,0x00001000,0x00041040,
    0x10001000,0x00000040,0x10000040,0x10040000,0x10040040,0x10000000,0x00040000,0x10001040,
    0x00000000,0x10041040,0x00040040,0x10000040,0x10040000,0x10001000,0x10001040,0x00000000,
    0x10041040,0x00041000,0x00041000,0x00001040,0x00001040,0x00040040,0x10000000,0x10041000
};

} // 匿名命名空间

static void scrunch(const uint8_t* in, uint32_t* out);
static void unscrun(const uint32_t* in, uint8_t* out);
static void desfunc(uint32_t* block, const uint32_t* keys);
static void cookey(const uint32_t* raw1);

void deskey(const uint8_t* key, int edf)
{
    uint8_t pc1m[56]{}, pcr[56]{};
    uint32_t kn[32]{};

    for (int j = 0; j < 56; j++) {
        int l = pc1[j];
        int m = l & 7;
        pc1m[j] = (key[l >> 3] & bytebit[m]) ? 1 : 0;
    }
    for (int i = 0; i < 16; i++) {
        int m = (edf == DE1) ? ((15 - i) << 1) : (i << 1);
        int n = m + 1;
        kn[m] = kn[n] = 0;
        for (int j = 0; j < 28; j++) {
            int l = j + totrot[i];
            pcr[j] = (l < 28) ? pc1m[l] : pc1m[l - 28];
        }
        for (int j = 28; j < 56; j++) {
            int l = j + totrot[i];
            pcr[j] = (l < 56) ? pc1m[l] : pc1m[l - 28];
        }
        for (int j = 0; j < 24; j++) {
            if (pcr[pc2[j]]) kn[m] |= bigbyte[j];
            if (pcr[pc2[j + 24]]) kn[n] |= bigbyte[j];
        }
    }
    cookey(kn);
}

void cookey(const uint32_t* raw1) 
{
    uint32_t dough[32];
    for (int i = 0; i < 16; i++) {
        const uint32_t* raw0 = raw1 + i * 2;
        dough[i * 2] = ((*raw0 & 0x00fc0000U) << 6)
                     | ((*raw0 & 0x00000fc0U) << 10)
                     | ((*(raw0 + 1) & 0x00fc0000U) >> 10)
                     | ((*(raw0 + 1) & 0x00000fc0U) >> 6);
        dough[i * 2 + 1] = ((*raw0 & 0x0003f000U) << 12)
                         | ((*raw0 & 0x0000003fU) << 16)
                         | ((*(raw0 + 1) & 0x0003f000U) >> 4)
                         | ((*(raw0 + 1) & 0x0000003fU));
    }
    std::memcpy(KnL, dough, sizeof(dough));
}

void cpkey(uint32_t* into)
{
    std::memcpy(into, KnL, sizeof(KnL));
}

void usekey(const uint32_t* from)
{
    std::memcpy(KnL, from, sizeof(KnL));
}

void des(const uint8_t* inblock, uint8_t* outblock)
{
    uint32_t work[2];
    scrunch(inblock, work);
    desfunc(work, KnL);
    unscrun(work, outblock);
}

void scrunch(const uint8_t* outof, uint32_t* into)
{
    into[0] = (uint32_t(outof[0]) << 24) | (uint32_t(outof[1]) << 16) |
              (uint32_t(outof[2]) << 8) | uint32_t(outof[3]);
    into[1] = (uint32_t(outof[4]) << 24) | (uint32_t(outof[5]) << 16) |
              (uint32_t(outof[6]) << 8) | uint32_t(outof[7]);
}

void unscrun(const uint32_t* outof, uint8_t* into)
{
    into[0] = uint8_t((outof[0] >> 24) & 0xff);
    into[1] = uint8_t((outof[0] >> 16) & 0xff);
    into[2] = uint8_t((outof[0] >> 8) & 0xff);
    into[3] = uint8_t(outof[0] & 0xff);
    into[4] = uint8_t((outof[1] >> 24) & 0xff);
    into[5] = uint8_t((outof[1] >> 16) & 0xff);
    into[6] = uint8_t((outof[1] >> 8) & 0xff);
    into[7] = uint8_t(outof[1] & 0xff);
}

void desfunc(uint32_t* block, const uint32_t* keys)
{
    uint32_t fval, work, right, leftt;
    int round;

    leftt = block[0];
    right = block[1];
    work = ((leftt >> 4) ^ right) & 0x0f0f0f0fU;
    right ^= work;
    leftt ^= (work << 4);
    work = ((leftt >> 16) ^ right) & 0x0000ffffU;
    right ^= work;
    leftt ^= (work << 16);
    work = ((right >> 2) ^ leftt) & 0x33333333U;
    leftt ^= work;
    right ^= (work << 2);
    work = ((right >> 8) ^ leftt) & 0x00ff00ffU;
    leftt ^= work;
    right ^= (work << 8);
    right = ((right << 1) | ((right >> 31) & 1U)) & 0xffffffffU;
    work = (leftt ^ right) & 0xaaaaaaaaU;
    leftt ^= work;
    right ^= work;
    leftt = ((leftt << 1) | ((leftt >> 31) & 1U)) & 0xffffffffU;

    for (round = 0; round < 8; round++) {
        work  = (right << 28) | (right >> 4);
        work ^= *keys++;
        fval  = SP7[ work                & 0x3fU];
        fval |= SP5[(work >>  8) & 0x3fU];
        fval |= SP3[(work >> 16) & 0x3fU];
        fval |= SP1[(work >> 24) & 0x3fU];
        work  = right ^ *keys++;
        fval |= SP8[ work                & 0x3fU];
        fval |= SP6[(work >>  8) & 0x3fU];
        fval |= SP4[(work >> 16) & 0x3fU];
        fval |= SP2[(work >> 24) & 0x3fU];
        leftt ^= fval;
        work  = (leftt << 28) | (leftt >> 4);
        work ^= *keys++;
        fval  = SP7[ work                & 0x3fU];
        fval |= SP5[(work >>  8) & 0x3fU];
        fval |= SP3[(work >> 16) & 0x3fU];
        fval |= SP1[(work >> 24) & 0x3fU];
        work  = leftt ^ *keys++;
        fval |= SP8[ work                & 0x3fU];
        fval |= SP6[(work >>  8) & 0x3fU];
        fval |= SP4[(work >> 16) & 0x3fU];
        fval |= SP2[(work >> 24) & 0x3fU];
        right ^= fval;
    }

    right = (right << 31) | (right >> 1);
    work = (leftt ^ right) & 0xaaaaaaaaU;
    leftt ^= work;
    right ^= work;
    leftt = (leftt << 31) | (leftt >> 1);
    work = ((leftt >> 8) ^ right) & 0x00ff00ffU;
    right ^= work;
    leftt ^= (work << 8);
    work = ((leftt >> 2) ^ right) & 0x33333333U;
    right ^= work;
    leftt ^= (work << 2);
    work = ((right >> 16) ^ leftt) & 0x0000ffffU;
    leftt ^= work;
    right ^= (work << 16);
    work = ((right >> 4) ^ leftt) & 0x0f0f0f0fU;
    leftt ^= work;
    right ^= (work << 4);
    block[0] = right;
    block[1] = leftt;
}

} // namespace