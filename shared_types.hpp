#pragma once

#include <cstdint>
#include <chrono>

namespace hft {

// Common type definitions used across all simulators
using Price = uint64_t;
using Quantity = uint64_t;
using OrderId = uint64_t;
using SymbolId = uint16_t;

// Fixed-point arithmetic constants
constexpr uint64_t PRICE_MULTIPLIER = 100000000ULL;
constexpr uint64_t QUANTITY_MULTIPLIER = 100000000ULL;

// Utility functions for price conversion
inline Price to_fixed_price(double price) {
    return static_cast<Price>(price * PRICE_MULTIPLIER);
}

inline double to_float_price(Price price) {
    return static_cast<double>(price) / PRICE_MULTIPLIER;
}

inline Quantity to_fixed_quantity(double quantity) {
    return static_cast<Quantity>(quantity * QUANTITY_MULTIPLIER);
}

inline double to_float_quantity(Quantity quantity) {
    return static_cast<double>(quantity) / QUANTITY_MULTIPLIER;
}

// Common timestamp utility
inline uint64_t get_timestamp_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

// Common enumerations
enum class Venue : uint8_t {
    COINBASE = 0,
    BINANCE = 1
};

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1
};

// Common venue utilities
inline const char* venue_to_string(Venue venue) {
    switch (venue) {
        case Venue::COINBASE: return "COINBASE";
        case Venue::BINANCE: return "BINANCE";
        default: return "UNKNOWN";
    }
}

inline const char* side_to_string(Side side) {
    switch (side) {
        case Side::BUY: return "BUY";
        case Side::SELL: return "SELL";
        default: return "UNKNOWN";
    }
}

// Common symbol definitions
constexpr SymbolId BTC_USD = 1;
constexpr SymbolId ETH_USD = 2;

inline const char* symbol_to_string(SymbolId symbol) {
    switch (symbol) {
        case BTC_USD: return "BTC/USD";
        case ETH_USD: return "ETH/USD";
        default: return "UNKNOWN";
    }
}

} // namespace hft
