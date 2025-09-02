#pragma once

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>
#include "../messages.hpp"

namespace hft::indicators {

// Simple circular buffer for efficient rolling calculations
template<typename T>
class RollingBuffer {
private:
    std::deque<T> data_;
    size_t max_size_;
    
public:
    explicit RollingBuffer(size_t max_size) : max_size_(max_size) {}
    
    void add(const T& value) {
        data_.push_back(value);
        if (data_.size() > max_size_) {
            data_.pop_front();
        }
    }
    
    bool is_full() const { return data_.size() == max_size_; }
    size_t size() const { return data_.size(); }
    const std::deque<T>& get_data() const { return data_; }
    T back() const { return data_.back(); }
    T front() const { return data_.front(); }
    
    void clear() { data_.clear(); }
};

// RSI (Relative Strength Index) Calculator
class RSI {
private:
    RollingBuffer<double> prices_;
    RollingBuffer<double> gains_;
    RollingBuffer<double> losses_;
    size_t period_;
    double avg_gain_ = 0.0;
    double avg_loss_ = 0.0;
    bool initialized_ = false;
    
public:
    explicit RSI(size_t period = 14) : prices_(period + 1), gains_(period), losses_(period), period_(period) {}
    
    void add_price(double price) {
        prices_.add(price);
        
        if (prices_.size() >= 2) {
            double change = prices_.back() - prices_.get_data()[prices_.size() - 2];
            double gain = change > 0 ? change : 0.0;
            double loss = change < 0 ? -change : 0.0;
            
            gains_.add(gain);
            losses_.add(loss);
            
            if (gains_.is_full() && losses_.is_full()) {
                if (!initialized_) {
                    // Initial calculation using simple average
                    avg_gain_ = std::accumulate(gains_.get_data().begin(), gains_.get_data().end(), 0.0) / period_;
                    avg_loss_ = std::accumulate(losses_.get_data().begin(), losses_.get_data().end(), 0.0) / period_;
                    initialized_ = true;
                } else {
                    // Subsequent calculations using Wilder's smoothing
                    avg_gain_ = (avg_gain_ * (period_ - 1) + gain) / period_;
                    avg_loss_ = (avg_loss_ * (period_ - 1) + loss) / period_;
                }
            }
        }
    }
    
    double get_rsi() const {
        if (!initialized_ || avg_loss_ == 0.0) {
            return 50.0; // Neutral RSI when no data or no losses
        }
        
        double rs = avg_gain_ / avg_loss_;
        return 100.0 - (100.0 / (1.0 + rs));
    }
    
    bool is_ready() const {
        return initialized_;
    }
    
    void reset() {
        prices_.clear();
        gains_.clear();
        losses_.clear();
        avg_gain_ = 0.0;
        avg_loss_ = 0.0;
        initialized_ = false;
    }
};

// VWAP (Volume Weighted Average Price) Calculator
class VWAP {
private:
    struct PriceVolume {
        double price;
        uint64_t volume;
        uint64_t timestamp;
    };
    
    std::vector<PriceVolume> data_;
    uint64_t session_start_time_ = 0;
    double cumulative_pv_ = 0.0;  // Price * Volume
    uint64_t cumulative_volume_ = 0;
    
public:
    VWAP() {
        reset_session();
    }
    
    void add_trade(double price, uint64_t volume, uint64_t timestamp = 0) {
        if (timestamp == 0) {
            timestamp = get_timestamp_ns();
        }
        
        // Reset for new session if needed (simplified - would need proper session detection)
        if (session_start_time_ == 0) {
            session_start_time_ = timestamp;
        }
        
        PriceVolume pv{price, volume, timestamp};
        data_.push_back(pv);
        
        cumulative_pv_ += price * volume;
        cumulative_volume_ += volume;
    }
    
    double get_vwap() const {
        return cumulative_volume_ > 0 ? cumulative_pv_ / cumulative_volume_ : 0.0;
    }
    
    uint64_t get_cumulative_volume() const {
        return cumulative_volume_;
    }
    
    void reset_session() {
        data_.clear();
        cumulative_pv_ = 0.0;
        cumulative_volume_ = 0;
        session_start_time_ = get_timestamp_ns();
    }
    
    bool has_data() const {
        return cumulative_volume_ > 0;
    }
};

// Standard Deviation Calculator (for volatility)
class StandardDeviation {
private:
    RollingBuffer<double> prices_;
    size_t period_;
    
public:
    explicit StandardDeviation(size_t period = 20) : prices_(period), period_(period) {}
    
    void add_price(double price) {
        prices_.add(price);
    }
    
    double get_std_dev() const {
        if (!prices_.is_full()) {
            return 0.0;
        }
        
        const auto& data = prices_.get_data();
        double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
        
        double sum_sq_diff = 0.0;
        for (double price : data) {
            double diff = price - mean;
            sum_sq_diff += diff * diff;
        }
        
        return std::sqrt(sum_sq_diff / data.size());
    }
    
    double get_mean() const {
        if (prices_.size() == 0) return 0.0;
        const auto& data = prices_.get_data();
        return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    }
    
    bool is_ready() const {
        return prices_.is_full();
    }
    
    void reset() {
        prices_.clear();
    }
};

// Simple Moving Average
class SimpleMovingAverage {
private:
    RollingBuffer<double> prices_;
    size_t period_;
    
public:
    explicit SimpleMovingAverage(size_t period) : prices_(period), period_(period) {}
    
    void add_price(double price) {
        prices_.add(price);
    }
    
    double get_sma() const {
        if (prices_.size() == 0) return 0.0;
        const auto& data = prices_.get_data();
        return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    }
    
    bool is_ready() const {
        return prices_.is_full();
    }
    
    void reset() {
        prices_.clear();
    }
};

// Exponential Moving Average
class ExponentialMovingAverage {
private:
    double ema_ = 0.0;
    double multiplier_;
    bool initialized_ = false;
    
public:
    explicit ExponentialMovingAverage(size_t period) {
        multiplier_ = 2.0 / (period + 1.0);
    }
    
    void add_price(double price) {
        if (!initialized_) {
            ema_ = price;
            initialized_ = true;
        } else {
            ema_ = (price * multiplier_) + (ema_ * (1.0 - multiplier_));
        }
    }
    
    double get_ema() const {
        return ema_;
    }
    
    bool is_ready() const {
        return initialized_;
    }
    
    void reset() {
        ema_ = 0.0;
        initialized_ = false;
    }
};

// Bollinger Bands
class BollingerBands {
private:
    SimpleMovingAverage sma_;
    StandardDeviation std_dev_;
    double multiplier_;
    
public:
    explicit BollingerBands(size_t period = 20, double std_multiplier = 2.0) 
        : sma_(period), std_dev_(period), multiplier_(std_multiplier) {}
    
    void add_price(double price) {
        sma_.add_price(price);
        std_dev_.add_price(price);
    }
    
    struct Bands {
        double upper = 0.0;
        double middle = 0.0;
        double lower = 0.0;
        bool is_valid = false;
    };
    
    Bands get_bands() const {
        Bands bands;
        if (sma_.is_ready() && std_dev_.is_ready()) {
            bands.middle = sma_.get_sma();
            double deviation = std_dev_.get_std_dev() * multiplier_;
            bands.upper = bands.middle + deviation;
            bands.lower = bands.middle - deviation;
            bands.is_valid = true;
        }
        return bands;
    }
    
    bool is_ready() const {
        return sma_.is_ready() && std_dev_.is_ready();
    }
    
    void reset() {
        sma_.reset();
        std_dev_.reset();
    }
};

// Comprehensive indicator manager for a symbol
class IndicatorManager {
private:
    RSI rsi_5_;
    RSI rsi_14_;
    VWAP vwap_;
    StandardDeviation std_dev_20_;
    SimpleMovingAverage sma_20_;
    BollingerBands bollinger_;
    
    // Price tracking
    double last_price_ = 0.0;
    double day_open_ = 0.0;
    double prev_close_ = 0.0;
    uint64_t total_volume_ = 0;
    uint64_t average_volume_30d_ = 1000000; // Default, would be loaded from historical data
    
public:
    IndicatorManager() : rsi_5_(5), rsi_14_(14), std_dev_20_(20), sma_20_(20), bollinger_(20, 2.0) {}
    
    void update(double price, uint64_t volume, uint64_t timestamp = 0) {
        last_price_ = price;
        
        // Update all indicators
        rsi_5_.add_price(price);
        rsi_14_.add_price(price);
        vwap_.add_trade(price, volume, timestamp);
        std_dev_20_.add_price(price);
        sma_20_.add_price(price);
        bollinger_.add_price(price);
        
        total_volume_ += volume;
    }
    
    void set_session_data(double open_price, double previous_close) {
        day_open_ = open_price;
        prev_close_ = previous_close;
        vwap_.reset_session();
        total_volume_ = 0;
    }
    
    void set_historical_volume(uint64_t avg_volume_30d) {
        average_volume_30d_ = avg_volume_30d;
    }
    
    // Getters for all indicators
    double get_rsi_5() const { return rsi_5_.get_rsi(); }
    double get_rsi_14() const { return rsi_14_.get_rsi(); }
    double get_vwap() const { return vwap_.get_vwap(); }
    double get_std_dev_20() const { return std_dev_20_.get_std_dev(); }
    double get_sma_20() const { return sma_20_.get_sma(); }
    
    double get_distance_from_vwap_percent() const {
        double vwap = get_vwap();
        return (vwap > 0 && last_price_ > 0) ? (last_price_ - vwap) / vwap : 0.0;
    }
    
    BollingerBands::Bands get_bollinger_bands() const {
        return bollinger_.get_bands();
    }
    
    // Session data
    double get_day_open() const { return day_open_; }
    double get_prev_close() const { return prev_close_; }
    uint64_t get_cumulative_volume() const { return total_volume_; }
    uint64_t get_average_volume_30d() const { return average_volume_30d_; }
    double get_last_price() const { return last_price_; }
    
    // Readiness checks
    bool indicators_ready() const {
        return rsi_5_.is_ready() && rsi_14_.is_ready() && 
               std_dev_20_.is_ready() && vwap_.has_data();
    }
    
    void reset_session() {
        vwap_.reset_session();
        total_volume_ = 0;
        // Note: Don't reset other indicators as they should persist across sessions
    }
    
    void reset_all() {
        rsi_5_.reset();
        rsi_14_.reset();
        vwap_.reset_session();
        std_dev_20_.reset();
        sma_20_.reset();
        bollinger_.reset();
        total_volume_ = 0;
        last_price_ = 0.0;
    }
};

} // namespace hft::indicators
