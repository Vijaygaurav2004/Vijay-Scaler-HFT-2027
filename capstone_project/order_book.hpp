#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

struct Order {
    uint64_t order_id;     // Unique order identifier
    bool is_buy;           // true = buy, false = sell
    double price;          // Limit price
    uint64_t quantity;     // Remaining quantity
    uint64_t timestamp_ns; // Order entry timestamp in nanoseconds
    
    // Additional fields for internal linking
    Order* next{nullptr};
    Order* prev{nullptr};
    bool is_active{true};

    Order() = default;
    Order(uint64_t id, bool buy, double p, uint64_t qty, uint64_t ts)
        : order_id(id), is_buy(buy), price(p), quantity(qty), timestamp_ns(ts) {}

    Order(const Order& other) 
        : order_id(other.order_id), is_buy(other.is_buy),
          price(other.price), quantity(other.quantity),
          timestamp_ns(other.timestamp_ns), next(nullptr), prev(nullptr),
          is_active(other.is_active) {}

    Order& operator=(const Order& other) {
        if (this != &other) {
            order_id = other.order_id;
            is_buy = other.is_buy;
            price = other.price;
            quantity = other.quantity;
            timestamp_ns = other.timestamp_ns;
            next = nullptr;
            prev = nullptr;
            is_active = other.is_active;
        }
        return *this;
    }
};

struct PriceLevel {
    double price;
    uint64_t total_quantity;
    
    PriceLevel() : price(0.0), total_quantity(0) {}
    PriceLevel(double p, uint64_t qty) : price(p), total_quantity(qty) {}
};

class OrderBook {
public:
    // Insert a new order into the book
    void add_order(const Order& order);

    // Cancel an existing order by its ID
    bool cancel_order(uint64_t order_id);

    // Amend an existing order's price or quantity
    bool amend_order(uint64_t order_id, double new_price, uint64_t new_quantity);

    // Get a snapshot of top N bid and ask levels (aggregated quantities)
    void get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const;

    // Print current state of the order book
    void print_book(size_t depth = 10) const;

    // Additional utility methods
    double get_best_bid() const;
    double get_best_ask() const;
    double get_spread() const;
    uint64_t get_version() const;
    size_t get_order_count() const;
    size_t get_bid_levels() const;
    size_t get_ask_levels() const;

    // Destructor
    ~OrderBook();

private:
    // Forward declarations for internal implementation
    struct InternalPriceLevel;
    template<typename T> class SimpleMemoryPool;
    
    // Internal implementation details
    class Impl;
    std::unique_ptr<Impl> pImpl;
    
public:
    OrderBook();
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = default;
    OrderBook& operator=(OrderBook&&) = default;
};
