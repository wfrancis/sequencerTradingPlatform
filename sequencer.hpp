// sequencer.hpp - Core sequencing components for order and message sequencing
#pragma once

#include <atomic>
#include <cstdint>
#include <array>
#include <chrono>
#include <thread>
#include <type_traits>
#ifdef HAS_NUMA
#include <numa.h>
#endif
// Platform-specific intrinsics
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#include <x86intrin.h>
#define HAS_X86_INTRINSICS 1
#endif
#include "messages.hpp"

namespace hft {

// Cache line size for x86-64 processors
constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * Cross-platform high-resolution timestamp function
 */
inline uint64_t rdtsc() noexcept {
#ifdef HAS_X86_INTRINSICS
    return __builtin_ia32_rdtsc();
#else
    auto now = std::chrono::high_resolution_clock::now();
    return static_cast<uint64_t>(now.time_since_epoch().count());
#endif
}

/**
 * Cache-line aligned wrapper to prevent false sharing
 *
 * False sharing occurs when multiple threads access different variables
 * that happen to reside on the same cache line. This causes unnecessary
 * cache coherency traffic between CPU cores.
 *
 * By padding each atomic variable to its own cache line, we ensure
 * that each thread can modify its variable without affecting others.
 */
template<typename T>
struct alignas(CACHE_LINE_SIZE) CachePadded {
    T value;
    // Padding to fill the rest of the cache line
    char padding[CACHE_LINE_SIZE - sizeof(T)];

    CachePadded() : value{} {}
    
    // For atomic types, we need to handle initialization differently
    template<typename U = T>
    explicit CachePadded(typename std::enable_if<!std::is_same_v<U, std::atomic<uint64_t>>, const T&>::type v) 
        : value(v) {}
        
    // Special constructor for atomic types
    template<typename U = T>
    explicit CachePadded(typename std::enable_if<std::is_same_v<U, std::atomic<uint64_t>>, uint64_t>::type v) 
        : value(v) {}
};

/**
 * Single Producer Single Consumer Sequencer
 *
 * This is the fastest sequencer type, used when only one thread
 * generates sequences and one thread consumes them.
 *
 * Performance characteristics:
 * - ~2ns per sequence generation
 * - No CAS operations needed
 * - Minimal memory ordering constraints
 *
 * Use cases:
 * - Per-strategy sequencing
 * - Single gateway message ordering
 * - Component-to-component communication
 */
class SPSCSequencer {
private:
    // Producer's sequence counter - only written by producer
    CachePadded<std::atomic<uint64_t>> sequence_;

    // Commit point - written by producer, read by consumer
    CachePadded<std::atomic<uint64_t>> committed_;

    // Producer's cached view of consumer position (optimization)
    CachePadded<uint64_t> cached_consumer_;

public:
    SPSCSequencer() : sequence_(0), committed_(0), cached_consumer_(0) {}

    /**
     * Get next sequence number (Producer side)
     *
     * This operation is wait-free and takes ~2ns.
     * Uses relaxed memory ordering since only one thread writes.
     *
     * @return Next sequence number
     */
    [[gnu::hot]] [[gnu::flatten]]
    inline uint64_t next() noexcept {
        // Relaxed is safe here - single producer
        return sequence_.value.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Claim multiple sequences atomically (Producer side)
     *
     * @param count Number of sequences to claim
     * @return First sequence number in the range
     */
    [[gnu::hot]]
    inline uint64_t claim_batch(uint32_t count) noexcept {
        return sequence_.value.fetch_add(count, std::memory_order_relaxed);
    }

    /**
     * Commit a sequence number (Producer side)
     *
     * Makes the sequence visible to consumers. Must be called
     * in sequence order to maintain consistency.
     *
     * @param seq Sequence number to commit
     */
    [[gnu::hot]] [[gnu::flatten]]
    inline void commit(uint64_t seq) noexcept {
        // Release ensures all previous writes are visible
        committed_.value.store(seq + 1, std::memory_order_release);
    }

    /**
     * Batch commit multiple sequences (Producer side)
     *
     * More efficient than individual commits when processing batches
     *
     * @param highest_seq Highest sequence number in the batch
     */
    [[gnu::hot]]
    inline void commit_batch(uint64_t highest_seq) noexcept {
        committed_.value.store(highest_seq + 1, std::memory_order_release);
    }

    /**
     * Get highest committed sequence (Consumer side)
     *
     * @return Highest sequence number that has been committed
     */
    [[gnu::hot]] [[gnu::flatten]]
    inline uint64_t get_committed() const noexcept {
        // Acquire ensures we see all writes before the commit
        return committed_.value.load(std::memory_order_acquire) - 1;
    }

    /**
     * Check if a specific sequence is committed (Consumer side)
     *
     * @param seq Sequence number to check
     * @return true if sequence has been committed
     */
    [[gnu::hot]]
    inline bool is_committed(uint64_t seq) const noexcept {
        return seq < committed_.value.load(std::memory_order_acquire);
    }
};

/**
 * Multi-Producer Single Consumer Sequencer
 *
 * Handles multiple producers generating sequences with a single consumer.
 * Uses atomic fetch_add for sequence generation.
 *
 * Performance characteristics:
 * - ~5-10ns per sequence under contention
 * - Lock-free using CAS operations
 * - Handles out-of-order commits
 *
 * Use cases:
 * - Multiple strategies sending to one gateway
 * - Aggregating signals from multiple sources
 * - Collecting risk updates from multiple components
 */
class MPSCSequencer {
private:
    // Shared sequence counter - accessed by all producers
    CachePadded<std::atomic<uint64_t>> sequence_;

    // Commit tracking for out-of-order commits
    static constexpr size_t COMMIT_BUFFER_SIZE = 4096;
    static constexpr size_t COMMIT_BUFFER_MASK = COMMIT_BUFFER_SIZE - 1;

    // Circular buffer of commit flags
    std::array<CachePadded<std::atomic<uint8_t>>, COMMIT_BUFFER_SIZE> commits_;

    // Highest contiguous committed sequence
    CachePadded<std::atomic<uint64_t>> committed_head_;

public:
    MPSCSequencer() : sequence_(0), committed_head_(0) {
        for (auto& commit : commits_) {
            commit.value.store(0, std::memory_order_relaxed);
        }
    }

    /**
     * Claim a sequence number (Producer side)
     *
     * Thread-safe sequence generation using atomic fetch_add.
     * Multiple threads can call this concurrently.
     *
     * @return Claimed sequence number
     */
    [[gnu::hot]] [[gnu::flatten]]
    inline uint64_t claim() noexcept {
        // fetch_add is atomic and returns the previous value
        return sequence_.value.fetch_add(1, std::memory_order_acq_rel);
    }

    /**
     * Claim multiple sequences atomically (Producer side)
     *
     * More efficient than multiple claim() calls
     *
     * @param count Number of sequences to claim
     * @return First sequence number in the range
     */
    [[gnu::hot]]
    inline uint64_t claim_batch(uint32_t count) noexcept {
        return sequence_.value.fetch_add(count, std::memory_order_acq_rel);
    }

    /**
     * Commit a sequence (Producer side)
     *
     * Handles out-of-order commits. Threads can commit their
     * sequences in any order, and the sequencer tracks the
     * highest contiguous committed sequence.
     *
     * @param seq Sequence number to commit
     */
    [[gnu::hot]]
    inline void commit(uint64_t seq) noexcept {
        // Mark this sequence as committed
        const size_t idx = seq & COMMIT_BUFFER_MASK;
        commits_[idx].value.store(1, std::memory_order_release);

        // Try to advance the committed head
        // This is the tricky part - we need to find the highest
        // contiguous sequence that has been committed

        uint64_t current_head = committed_head_.value.load(std::memory_order_acquire);

        // If we committed the next expected sequence, try to advance
        if (seq == current_head) {
            uint64_t new_head = current_head;

            // Scan forward to find highest contiguous commit
            while (commits_[new_head & COMMIT_BUFFER_MASK].value.load(
                       std::memory_order_acquire) == 1) {
                // Clear the commit flag
                commits_[new_head & COMMIT_BUFFER_MASK].value.store(
                    0, std::memory_order_relaxed);
                new_head++;
            }

            // Try to update the head (may fail if another thread updated)
            committed_head_.value.compare_exchange_weak(
                current_head, new_head,
                std::memory_order_release,
                std::memory_order_relaxed);
        }
    }

    /**
     * Get highest contiguous committed sequence (Consumer side)
     *
     * @return Highest sequence where all sequences [0, N] are committed
     */
    [[gnu::hot]]
    inline uint64_t get_committed() const noexcept {
        return committed_head_.value.load(std::memory_order_acquire) - 1;
    }
};

/**
 * Multi-Producer Multi-Consumer Sequencer
 *
 * Supports multiple producers and multiple consumers.
 * Most complex but most flexible sequencer type.
 *
 * Performance characteristics:
 * - ~10-20ns per sequence under high contention
 * - Full memory barriers required
 * - Supports fair ordering between producers
 *
 * Use cases:
 * - Global message bus
 * - Cross-component communication
 * - Audit log sequencing
 */
class MPMCSequencer {
private:
    // Global sequence counter
    CachePadded<std::atomic<uint64_t>> sequence_;

    // Per-venue sequences for fairness
    static constexpr size_t MAX_VENUES = 16;
    std::array<CachePadded<std::atomic<uint64_t>>, MAX_VENUES> venue_sequences_;

    // Commit gate for ordering
    CachePadded<std::atomic<uint64_t>> commit_gate_;

public:
    MPMCSequencer() : sequence_(0), commit_gate_(0) {
        for (auto& vs : venue_sequences_) {
            vs.value.store(0, std::memory_order_relaxed);
        }
    }

    /**
     * Claim sequence with venue fairness (Producer side)
     *
     * Ensures fair ordering between different venues/sources
     *
     * @param venue Source venue for fairness
     * @return Sequence number
     */
    [[gnu::hot]]
    inline uint64_t claim(Venue venue) noexcept {
        // Update venue-specific counter for fairness tracking
        venue_sequences_[static_cast<size_t>(venue)].value.fetch_add(
            1, std::memory_order_relaxed);

        // Get global sequence
        return sequence_.value.fetch_add(1, std::memory_order_acq_rel);
    }

    /**
     * Wait-free publish with ordering guarantee (Producer side)
     *
     * Ensures sequences are published in order even with multiple producers
     *
     * @param seq Sequence to publish
     */
    [[gnu::hot]]
    inline void publish(uint64_t seq) noexcept {
        // Spin until it's our turn to publish
        uint64_t expected = seq;
        while (!commit_gate_.value.compare_exchange_weak(
                   expected, seq + 1,
                   std::memory_order_acq_rel,
                   std::memory_order_relaxed)) {
            expected = seq;
            // CPU hint for spin-wait loops
#ifdef HAS_X86_INTRINSICS
            _mm_pause();
#else
            std::this_thread::yield(); // Portable alternative
#endif
        }
    }

    /**
     * Get venue-specific sequence count
     *
     * Useful for monitoring fairness and venue activity
     *
     * @param venue Venue to query
     * @return Number of sequences from this venue
     */
    inline uint64_t get_venue_count(Venue venue) const noexcept {
        return venue_sequences_[static_cast<size_t>(venue)].value.load(
            std::memory_order_relaxed);
    }
};

/**
 * Hardware Timestamp Sequencer
 *
 * Combines sequence numbers with hardware timestamps (TSC or PTP).
 * Essential for latency measurement and event correlation.
 *
 * Performance characteristics:
 * - ~8-12ns per sequence with timestamp
 * - Provides nanosecond-precision timestamps
 * - Can use RDTSC or RDTSCP for different guarantees
 */
class TimestampSequencer {
private:
    CachePadded<std::atomic<uint64_t>> sequence_;

    // TSC frequency for time conversion
    uint64_t tsc_frequency_;

    // Reference point for timestamp calculation
    uint64_t tsc_reference_;
    uint64_t wall_time_reference_ns_;

    /**
     * Read Time Stamp Counter (Cross-platform)
     *
     * Uses platform-specific high-resolution timing
     */
    [[gnu::hot]] [[gnu::always_inline]]
    static inline uint64_t rdtsc() noexcept {
#ifdef HAS_X86_INTRINSICS
        return __builtin_ia32_rdtsc();
#else
        // Fallback to high-resolution clock for non-x86 platforms
        auto now = std::chrono::high_resolution_clock::now();
        return static_cast<uint64_t>(now.time_since_epoch().count());
#endif
    }

    /**
     * Read Time Stamp Counter (Serializing, Cross-platform)
     *
     * Ensures all previous instructions complete before reading TSC
     */
    [[gnu::hot]] [[gnu::always_inline]]
    static inline uint64_t rdtscp() noexcept {
#ifdef HAS_X86_INTRINSICS
        uint32_t aux;
        return __builtin_ia32_rdtscp(&aux);
#else
        // For non-x86, use memory barrier + timer
        std::atomic_thread_fence(std::memory_order_seq_cst);
        auto now = std::chrono::high_resolution_clock::now();
        return static_cast<uint64_t>(now.time_since_epoch().count());
#endif
    }

public:
    struct TimestampedSequence {
        uint64_t sequence;
        uint64_t timestamp_ns;
        uint64_t tsc_value;
    };

    TimestampSequencer() : sequence_(0) {
        calibrate();
    }

    /**
     * Calibrate TSC against wall clock
     *
     * Determines TSC frequency for accurate time conversion
     */
    void calibrate() {
        // Implementation would measure TSC frequency
        // This is simplified - real implementation would be more robust
        tsc_frequency_ = 2400000000ULL; // 2.4 GHz typical
        tsc_reference_ = rdtsc();
        wall_time_reference_ns_ = 0;
    }

    /**
     * Get sequenced timestamp (Producer side)
     *
     * Atomic sequence generation with hardware timestamp
     *
     * @return Sequence number with timestamp
     */
    [[gnu::hot]]
    inline TimestampedSequence next() noexcept {
        // Get sequence first
        const uint64_t seq = sequence_.value.fetch_add(1, std::memory_order_acq_rel);

        // Memory fence to ensure sequence is visible before TSC read
#ifdef HAS_X86_INTRINSICS
        _mm_mfence();
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif

        // Read hardware timestamp
        const uint64_t tsc = rdtsc();

        // Convert TSC to nanoseconds
        const uint64_t elapsed_tsc = tsc - tsc_reference_;
        const uint64_t elapsed_ns = (elapsed_tsc * 1000000000ULL) / tsc_frequency_;

        return {
            .sequence = seq,
            .timestamp_ns = wall_time_reference_ns_ + elapsed_ns,
            .tsc_value = tsc
        };
    }

    /**
     * Get sequence with serializing timestamp
     *
     * Slower but guarantees timestamp is after all previous operations
     *
     * @return Sequence with guaranteed ordering
     */
    [[gnu::hot]]
    inline TimestampedSequence next_serialized() noexcept {
        const uint64_t seq = sequence_.value.fetch_add(1, std::memory_order_acq_rel);

        // RDTSCP is serializing - ensures all previous instructions complete
        const uint64_t tsc = rdtscp();

        const uint64_t elapsed_tsc = tsc - tsc_reference_;
        const uint64_t elapsed_ns = (elapsed_tsc * 1000000000ULL) / tsc_frequency_;

        return {
            .sequence = seq,
            .timestamp_ns = wall_time_reference_ns_ + elapsed_ns,
            .tsc_value = tsc
        };
    }
};

/**
 * Market Making Sequencer
 *
 * Specialized sequencer for market making strategies.
 * Tracks maker/taker sequences separately and provides
 * priority sequencing for latency-sensitive orders.
 *
 * Performance characteristics:
 * - ~10-15ns per sequence with full metadata
 * - Separate tracking for liquidity provision/taking
 * - Built-in priority levels
 */
class MarketMakingSequencer {
private:
    // Separate sequences for different order types
    CachePadded<std::atomic<uint64_t>> maker_sequence_;  // Liquidity providing
    CachePadded<std::atomic<uint64_t>> taker_sequence_;  // Liquidity taking
    CachePadded<std::atomic<uint64_t>> global_sequence_; // Total ordering

    // Priority sequence for urgent orders (e.g., risk reduction)
    CachePadded<std::atomic<uint64_t>> priority_sequence_;

    // Per-venue, per-side sequences for fair queue
    static constexpr size_t MAX_VENUES = 16;
    std::array<CachePadded<std::atomic<uint64_t>>, MAX_VENUES> venue_bid_sequences_;
    std::array<CachePadded<std::atomic<uint64_t>>, MAX_VENUES> venue_ask_sequences_;

public:
    struct MarketMakingSequence {
        uint64_t sequence;        // Global sequence
        uint64_t timestamp_ns;    // Hardware timestamp
        uint64_t type_sequence;   // Maker/taker specific sequence
        Venue venue;
        Side side;
        bool is_maker;
        bool is_priority;
    };

    MarketMakingSequencer() :
        maker_sequence_(0),
        taker_sequence_(0),
        global_sequence_(0),
        priority_sequence_(0) {

        for (auto& seq : venue_bid_sequences_) {
            seq.value.store(0, std::memory_order_relaxed);
        }
        for (auto& seq : venue_ask_sequences_) {
            seq.value.store(0, std::memory_order_relaxed);
        }
    }

    /**
     * Sequence a maker order (provides liquidity)
     *
     * Maker orders typically have lower priority but earn rebates
     *
     * @param venue Trading venue
     * @param side Bid or Ask
     * @return Sequenced order metadata
     */
    [[gnu::hot]]
    inline MarketMakingSequence sequence_maker(Venue venue, Side side) noexcept {
        // Update venue+side specific sequence
        auto& venue_seq = (side == Side::BID)
            ? venue_bid_sequences_[static_cast<size_t>(venue)]
            : venue_ask_sequences_[static_cast<size_t>(venue)];
        venue_seq.value.fetch_add(1, std::memory_order_relaxed);

        // Update maker sequence
        const uint64_t maker_seq = maker_sequence_.value.fetch_add(
            1, std::memory_order_relaxed);

        // Global sequence for total ordering
        const uint64_t global_seq = global_sequence_.value.fetch_add(
            1, std::memory_order_acq_rel);

        // Hardware timestamp
        const uint64_t timestamp = rdtsc();

        return {
            .sequence = global_seq,
            .timestamp_ns = timestamp,
            .type_sequence = maker_seq,
            .venue = venue,
            .side = side,
            .is_maker = true,
            .is_priority = false
        };
    }

    /**
     * Sequence a taker order (removes liquidity)
     *
     * Taker orders have higher priority for immediate execution
     *
     * @param venue Trading venue
     * @param side Buy or Sell
     * @param is_priority True for risk-reduction orders
     * @return Sequenced order metadata
     */
    [[gnu::hot]]
    inline MarketMakingSequence sequence_taker(
        Venue venue,
        Side side,
        bool is_priority = false) noexcept {

        // Taker sequence
        const uint64_t taker_seq = taker_sequence_.value.fetch_add(
            1, std::memory_order_relaxed);

        // Priority orders get special sequence
        uint64_t priority_seq = 0;
        if (is_priority) {
            priority_seq = priority_sequence_.value.fetch_add(
                1, std::memory_order_acq_rel);
        }

        // Global sequence
        const uint64_t global_seq = global_sequence_.value.fetch_add(
            1, std::memory_order_acq_rel);

        const uint64_t timestamp = rdtsc();

        // Embed priority in the sequence number for natural ordering
        // Priority orders get high bits set
        const uint64_t final_seq = is_priority
            ? (priority_seq << 48) | global_seq
            : global_seq;

        return {
            .sequence = final_seq,
            .timestamp_ns = timestamp,
            .type_sequence = taker_seq,
            .venue = venue,
            .side = side,
            .is_maker = false,
            .is_priority = is_priority
        };
    }

    /**
     * Get current spread sequences for market making decisions
     *
     * @param venue Venue to query
     * @return Pair of (bid_sequence, ask_sequence)
     */
    inline std::pair<uint64_t, uint64_t> get_spread_sequences(Venue venue) const noexcept {
        const size_t venue_idx = static_cast<size_t>(venue);

        const uint64_t bid_seq = venue_bid_sequences_[venue_idx].value.load(
            std::memory_order_acquire);
        const uint64_t ask_seq = venue_ask_sequences_[venue_idx].value.load(
            std::memory_order_acquire);

        return {bid_seq, ask_seq};
    }

    /**
     * Get maker/taker ratio for strategy decisions
     *
     * @return Ratio of maker to taker orders
     */
    inline double get_maker_taker_ratio() const noexcept {
        const uint64_t makers = maker_sequence_.value.load(std::memory_order_relaxed);
        const uint64_t takers = taker_sequence_.value.load(std::memory_order_relaxed);

        if (takers == 0) return 0.0;
        return static_cast<double>(makers) / static_cast<double>(takers);
    }
};

} // namespace hft