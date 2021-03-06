/** \file
    \brief CCatCodec
    \copyright Copyright (c) 2018 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of CCat nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include "CCatTools.h"

// Define this to use less memory but a bit more CPU
//#define CCAT_FREE_UNUSED_PACKETS

namespace ccat {


//------------------------------------------------------------------------------
// EncoderWindowElement

struct EncoderWindowElement
{
    // Send time for this packet
    Counter64 SendUsec = 0;

    // Size of data in bytes
    unsigned Bytes = 0;

    // Data for packet that is prepended with data size
    AlignedLightVector Data;
};


//------------------------------------------------------------------------------
// CCatEncoder

class CCatEncoder
{
public:
    // Dependencies
    const CCatSettings* SettingsPtr = nullptr;
    pktalloc::Allocator* AllocPtr = nullptr;

    // API
    CCatResult EncodeOriginal(const CCatOriginal& original);
    CCatResult EncodeRecovery(CCatRecovery& recoveryOut);

private:
    /// Preallocated window of packets
    EncoderWindowElement Window[kMaxEncoderWindowSize];

    /// Next window index to write to
    unsigned NextIndex = 0;

    /// Count of window elements
    unsigned Count = 0;

    /// Recovery packet generated by EncodeRecovery()
    AlignedLightVector RecoveryData;

    /// Next original packet sequence number
    Counter64 NextSequence = 0;

    /// Next matrix column
    uint8_t NextColumn = 0;

    /// Next matrix row to generate in EncodeRecovery()
    uint8_t NextRow = 1;

    /// Next sequence number that will use xor parity
    Counter64 NextParitySequence = 0;

    /// Last time an original packet was passed to EncodeOriginal()
    Counter64 LastOriginalSendUsec = 0;
};


//------------------------------------------------------------------------------
// RecoveryPacket

struct RecoveryPacket
{
    /// Next recovery packet in the sorted list with a higher sequence number
    RecoveryPacket* Next = nullptr;

    /// Previous recovery packet in the sorted list with a lower sequence number
    RecoveryPacket* Prev = nullptr;

    /// Recovery packet data.  Allocated with PacketAllocator
    uint8_t* Data = nullptr;

    /// Bytes in packet data
    unsigned Bytes = 0;

    /// Start of recovery span
    Counter64 SequenceStart = 0;

    /// End of recovery span (non-inclusive) = Last sequence number + 1
    Counter64 SequenceEnd = 0;

    /// Matrix row number
    uint8_t MatrixRow = 0;
};


//------------------------------------------------------------------------------
// OriginalPacket

struct OriginalPacket
{
    /// Pointer to packet data prepended with length field
    uint8_t* Data = nullptr;

    /// Bytes of data including the prepended length field
    unsigned Bytes = 0;
};


//------------------------------------------------------------------------------
// CCatDecoder

/**
    Tracks losses in the decoder original packet window in a compact, efficient
    way.  Alternative approaches are packed arrays that need to be recreated on
    new data, linked lists that have poor cache locality, or scanning for the
    sparse losses in the array of original data.  The Loss Window uses one bit
    for each original packet and provides methods that are useful for checking
    for possible solutions when a large number of recovery packets is received.

    Newer packets have higher sequence numbers.
    In the bitfield, the first word contains the lowest window sequence number.
    And the first bit of the first word indicates that the lowest sequence
    number is lost if it is a '1'.

    When the window is shifted to accomodate newer packets, it shifts the high
    bits into the lower bits.  Instead of shifting with every new packet, it
    will only shift in multiples of 64 bits to simplify this maintenance.
*/

class CCatDecoder
{
public:
    const CCatSettings* SettingsPtr = nullptr;
    pktalloc::Allocator* AllocPtr = nullptr;
    // Note: Allocator automatically frees all allocated memory on dtor
    // so there is no need to explicitly free any memory on dtor.

    CCatResult DecodeOriginal(const CCatOriginal& original);
    CCatResult DecodeRecovery(const CCatRecovery& recovery);

    CCAT_FORCE_INLINE CCatDecoder()
    {
        // All packets are lost initially
        Lost.SetAll();
    }

private:
    //--------------------------------------------------------------------------
    // Original/recovery data:

    /// Bitfield - 1 bits mean a loss at that offset from SequenceBase.
    /// Bits we have not received yet will also be marked with a 1.
    pktalloc::CustomBitSet<kDecoderWindowSize> Lost;

    /// Ring buffer of packet data
    OriginalPacket Packets[kDecoderWindowSize];

    /// Rotation of packets ring buffer
    unsigned PacketsRotation = 0;

    /// First sequence number in the window
    Counter64 SequenceBase = 0;

    /// Largest sequence number in the window + 1
    Counter64 SequenceEnd = 0;

    /// Recovery packet with the smallest sequence number.
    /// Stores up to kMaxDecoderRows recovery rows
    RecoveryPacket* RecoveryFirst = nullptr;

    /// Recovery packet with the largest sequence number
    RecoveryPacket* RecoveryLast = nullptr;


    //--------------------------------------------------------------------------
    // Solver state for 2+ losses recovered at a time:

    /// Number of bytes used for each recovery packet in the matrix.
    /// This is also the maximum size of all recovery packets in the set
    unsigned SolutionBytes = 0;

    /// Number of rows in matrix <= kMaxRecoveryRows
    unsigned RowCount = 0;

    /// Solver state: Information about recovery data for each row
    struct {
        /// Recovery packet for this row
        RecoveryPacket* Recovery;

        /// First lost column this one has
        unsigned ColumnStart;

        /// One beyond the last lost column this one covers
        unsigned ColumnEnd;
    } RowInfo[kMaxRecoveryColumns];

    /// Number of columns in matrix <= kMaxRecoveryColumns
    unsigned ColumnCount = 0;

    /// Solver state: Information about original data for each column
    struct {
        /// Sequence number for this original packet
        Counter64 Sequence;

        /// Pointer to original packet we will modify in-place
        OriginalPacket* OriginalPtr;
    } ColumnInfo[kMaxRecoveryRows];

    /// Generator row values
    uint8_t CauchyRows[kMaxRecoveryRows];

    /// Generator column values
    uint8_t CauchyColumns[kMaxRecoveryColumns];

    /// Solution matrix
    AlignedLightVector Matrix;

    /// Pivot row index for each matrix column
    uint8_t PivotRowIndex[kMaxRecoveryColumns];

    /// Data that starts out as per-row data but becomes solved column data
    uint8_t* DiagonalData[kMaxRecoveryColumns];


    //--------------------------------------------------------------------------
    // Statistics:

    /// Sequence number we failed to recover
    Counter64 FailureSequence = 0;

    /// Number of 2x2 or larger solves that succeeded
    uint64_t LargeRecoverySuccesses = 0;

    /// Number of 2x2 or larger solves that failed
    uint64_t LargeRecoveryFailures = 0;


    //--------------------------------------------------------------------------
    // Original/recovery data:

    /// Expand window to contain given original data span.
    enum class Expand
    {
        InWindow,
        OutOfWindow,
        Evacuated,
        Shifted
    };
    Expand ExpandWindow(Counter64 sequenceStart, unsigned count = 1);

    /// Clear recovery list
    void ClearRecoveryList();

    /// Remove recovery packets from the front that reference unavailable data
    void CleanupRecoveryList();

    /// Look up packet at a given 0-based element.
    /// Applies Rotation to the ring buffer to arrive at the actual location.
    CCAT_FORCE_INLINE OriginalPacket* GetPacket(unsigned element)
    {
        CCAT_DEBUG_ASSERT(element < kDecoderWindowSize);
        CCAT_DEBUG_ASSERT(PacketsRotation < kDecoderWindowSize);

        // Increment element by rotation modulo window size
        element += PacketsRotation;
        if (element >= kDecoderWindowSize)
            element -= kDecoderWindowSize;

        return &Packets[element];
    }

    /// Store original data in window
    CCatResult StoreOriginal(const CCatOriginal& original);

    /// Insert recovery packet into sorted list
    CCatResult StoreRecovery(const CCatRecovery& recovery);

    CCAT_FORCE_INLINE unsigned GetLostInRange(
        Counter64 sequenceStart,
        Counter64 sequenceEnd)
    {
        CCAT_DEBUG_ASSERT(sequenceStart >= SequenceBase);
        CCAT_DEBUG_ASSERT(sequenceEnd <= SequenceEnd);
        const unsigned start = (unsigned)(sequenceStart - SequenceBase).ToUnsigned();
        const unsigned end = (unsigned)(sequenceEnd - SequenceBase).ToUnsigned();
        return Lost.RangePopcount(start, end);
    }

    /// Solve common case when recovery row is only missing one original
    CCatResult SolveLostOne(const CCatRecovery& recovery);

    /// Check for a recovery packet in the list containing the given sequence
    /// number which now references just one loss.  Then call FindSolutions().
    CCatResult FindSolutionsContaining(const Counter64 sequence);


    //--------------------------------------------------------------------------
    // Solver state for 2+ losses recovered at a time:

    /// Find solutions starting from the right (latest) side of the matrix
    CCatResult FindSolutions();

    /// Solve the given span
    CCatResult Solve(RecoveryPacket* spanStart, RecoveryPacket* spanEnd);

    /// Generate arrays from spans as a first step to planning a solution
    CCatResult ArraysFromSpans(RecoveryPacket* spanStart, RecoveryPacket* spanEnd);

    /// Plan out steps that will recover the original data.
    /// Return CCat_Success if solution is possible (99.9% of the time).
    /// Return CCat_NeedsMoreData if given rows are insufficient to find a solution
    CCatResult PlanSolution();

    /// Gaussian elimination to put matrix in upper triangular form
    CCatResult ResumeGaussianElimination(
        uint8_t* pivot_data,
        unsigned pivotColumn);

    /// If a diagonal was zero, attempt to re-order the rows to solve it.
    CCatResult PivotedGaussianElimination(unsigned pivotColumn);

    /// Eliminate received original data from recovery rows used for solution.
    /// This is done after PlanSolution() and before ExecuteSolution()
    CCatResult EliminateOriginals();

    /// Execute solution plan to recover the original data
    void ExecuteSolutionPlan();

    /// Report solution to app
    CCatResult ReportSolution();

    /// Release exhausted recovery span
    void ReleaseSpan(
        RecoveryPacket* spanStart,
        RecoveryPacket* spanEnd,
        CCatResult solveResult);
};


//------------------------------------------------------------------------------
// CCatSession

class CCatSession
    : public CCatEncoder
    , public CCatDecoder
{
public:
    CCatResult Create(const CCatSettings& settings);

private:
    CCatSettings Settings;
    pktalloc::Allocator Alloc;
};


} // namespace ccat
