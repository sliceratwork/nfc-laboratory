/*

  Copyright (c) 2021 Jose Vicente Campos Martinez - <josevcm@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#ifndef NFC_NFC_H
#define NFC_NFC_H

namespace nfc {

enum TechType
{
   None = 0,
   NfcA = 1,
   NfcB = 2,
   NfcF = 3,
   NfcV = 4
};

enum RateType
{
   r106k = 0,
   r212k = 1,
   r424k = 2,
   r848k = 3
};

enum FrameType
{
   CarrierOff = 0,
   CarrierOn = 1,
   PollFrame = 2,
   ListenFrame = 3
};

enum FrameFlags
{
   ShortFrame = 0x01,
   Encrypted = 0x02,
   Truncated = 0x08,
   ParityError = 0x10,
   CrcError = 0x20,
   SyncError = 0x40
};

enum FramePhase
{
   CarrierFrame = 0,
   SelectionFrame = 1,
   ApplicationFrame = 2
};

// Frequency of operating field (carrier frequency) in Hz
constexpr float NFC_FC = 13.56E6;

// Frequency of subcarrier modulation in Hz
constexpr float NFC_FS = NFC_FC / 16;

// Elementary time unit
constexpr float NFC_ETU = 128 / NFC_FC;

// Activation frame waiting time, in 1/FS units
constexpr int NFC_FWT_ACTIVATION = 71680;

// FSDI to FSD conversion (frame size)
constexpr int NFC_FDS_TABLE[] = {16, 24, 32, 40, 48, 64, 96, 128, 256, 512, 1024, 2048, 4096, 0, 0, 0};

// Start-up Frame Guard Time, SFGT = 256 x 16 * (2 ^ SFGI) in 1/fc units
constexpr int NFC_SFGT_TABLE[] = {4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304, 8388608, 16777216, 33554432, 67108864, 134217728};

// Frame waiting time FWT = 256 x 16 * (2 ^ FWI) in 1/fc units
constexpr int NFC_FWT_TABLE[] = {4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304, 8388608, 16777216, 33554432, 67108864, 134217728};

/*
 * NFC-A parameters
 */

// NFC-A Default guard time between the end of a PCD transmission and the start of the PICC subcarrier generation in 1/FC units
constexpr int NFCA_FGT_DEF = 1024;

// NFC-A Default Frame Waiting Time
constexpr int NFCA_FWT_DEF = 256 * 16 * (1 << 4);

// NFC-A Default Start-up Frame Guard Time
constexpr int NFCA_SFGT_DEF = 256 * 16 * (1 << 0);

// NFC-A Default Request Guard Time
constexpr int NFCA_RGT_DEF = 7000;

// NFC-A Maximum frame Waiting Time for ATQA response
constexpr int NFCA_FWT_ATQA = 128 * 18;

/*
 * NFC-B parameters
 */

// NFC-B Guard time between the end of a PCD transmission and the start of the PICC subcarrier generation in 1/FC units
constexpr int NFCB_TR0_MIN = 1024;

// NFC-B Synchronization time between the start of the PICC subcarrier generation and the start of the PICC subcarrier modulation in 1/FC units
constexpr int NFCB_TR1_MIN = 1024;

// NFC-B Synchronization time between the start of the PICC subcarrier generation and the start of the PICC subcarrier modulation in 1/FC units
constexpr int NFCB_TR1_MAX = 3200;

// NFC-B Start of Sequence first modulation
constexpr int NFCB_TLISTEN_S1_MIN = 1272;

// NFC-B Start of Sequence first modulation
constexpr int NFCB_TLISTEN_S1_MAX = 1416;

// NFC-B Start of Sequence first modulation
constexpr int NFCB_TLISTEN_S2_MIN = 248;

// NFC-B Start of Sequence first modulation
constexpr int NFCB_TLISTEN_S2_MAX = 392;

// NFC-B Default Request Guard Time
constexpr int NFCB_FGT_DEF = NFCB_TR0_MIN;

// NFC-B Default Frame Waiting Time
constexpr int NFCB_FWT_DEF = 256 * 16 * (1 << 4);

// NFC-B Default Start-up Frame Guard Time
constexpr int NFCB_SFGT_DEF = 256 * 16 * (1 << 0);

// NFC-B Default Request Guard Time, defined as the minimum time between the start bits of two consecutive REQA commands.
constexpr int NFCB_RGT_DEF = 7000;

// NFC-B Frame Waiting Time for ATQB response
constexpr int NFCB_FWT_ATQB = 7680;

// NFC-B Number of Slots
constexpr int NFCB_SLOT_TABLE[] = {1, 2, 4, 8, 16, 0, 0, 0};

// NFC-B TR0min, in 1/FC units
constexpr int NFCB_TR0_MIN_TABLE[] = {0, 48 * 16, 16 * 16, 0};

// NFC-B TR1min, in 1/FC units
constexpr int NFCB_TR1_MIN_TABLE[] = {0, 64 * 16, 16 * 16, 0};

/*
 * NFC-F parameters
 */

// NFC-F Default Request Guard Time
constexpr int NFCF_FGT_DEF = 1024;

// NFC-F Default Frame Waiting Time
constexpr int NFCF_FWT_DEF = 256 * 16 * (1 << 4);

// NFC-F Default Start-up Frame Guard Time
constexpr int NFCF_SFGT_DEF = 4096;

// NFC-F Default Request Guard Time, defined as the minimum time between the start bits of two consecutive REQC commands.
constexpr int NFCF_RGT_DEF = 7000;

// NFC-F Frame Delay Time for ATQC response, between the end of the Request Frame and the first Time Slot
constexpr int NFCF_FDT_ATQC = 512 * 64;

// NFC-F Time Slot Unit for ATQC
constexpr int NFCF_TSU_ATQC = 256 * 64;

/*
 * NFC-V parameters
 */

// NFC-V Guard time between the end of a PCD transmission and the start of the PICC subcarrier generation in 1/FC units
constexpr int NFCV_TR0_MIN = 1024;

// NFC-V Default Request Guard Time
constexpr int NFCV_FGT_DEF = NFCV_TR0_MIN;

// NFC-V Default Request Guard Time
constexpr int NFCV_TLISTEN_S1 = 768;

// NFC-V Default Request Guard Time
constexpr int NFCV_TLISTEN_S2 = 256;

// NFC-V Default Frame Waiting Time
constexpr int NFCV_FWT_DEF = 256 * 16 * (1 << 4);

// NFC-V Default Start-up Frame Guard Time
constexpr int NFCV_SFGT_DEF = 4096;

// NFC-V Default Request Guard Time, defined as the minimum time between the start bits of two consecutive REQV commands.
constexpr int NFCV_RGT_DEF = 7000;

}

#endif //NFC_LAB_NFC_H
