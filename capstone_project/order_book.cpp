#include "order_book.hpp"
#include <map>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <limits>
#include <cassert>
#include <cmath>

using Price = double;

constexpr size_t MEMORY_POOL_BLOCK_SIZE = 1024;
constexpr size_t MAX_ORDER_QUANTITY = 1000000;
constexpr double MIN_PRICE = 0.01;
constexpr double MAX_PRICE = 1000000.0;

template<typename T>
class SimpleMemoryPool {
private:
    static constexpr size_t BLOCK_SIZE = MEMORY_POOL_BLOCK_SIZE;
    std::vector<std::unique_ptr<T[]>> blocks_;
    size_t current_block_index_{0};
    size_t current_position_{0};
    T* free_list_head_{nullptr};

public:
    SimpleMemoryPool() {
        allocate_new_block();
    }

    T* allocate() {
        T* ptr = pop_free_list();
        if (ptr) {
            return ptr;
        }

        if (current_position_ >= BLOCK_SIZE) {
            allocate_new_block();
        }

        return &blocks_[current_block_index_][current_position_++];
    }

    void deallocate(T* ptr) {
        if (ptr) {
            push_free_list(ptr);
        }
    }

private:
    void allocate_new_block() {
        try {
            blocks_.emplace_back(std::make_unique<T[]>(BLOCK_SIZE));
            current_block_index_ = blocks_.size() - 1;
            current_position_ = 0;
        } catch (const std::bad_alloc& e) {
            std::cerr << "Error: Failed to allocate memory block: " << e.what() << "\n";
            throw;
        }
    }

    T* pop_free_list() {
        if (free_list_head_) {
            T* head = free_list_head_;
            free_list_head_ = *reinterpret_cast<T**>(head);
            return head;
        }
        return nullptr;
    }

    void push_free_list(T* ptr) {
        *reinterpret_cast<T**>(ptr) = free_list_head_;
        free_list_head_ = ptr;
    }
};

struct InternalPriceLevel {
    double price;
    uint64_t total_quantity{0};
    Order* first_order{nullptr};
    Order* last_order{nullptr};
    size_t order_count{0};
    bool is_active{true};

    InternalPriceLevel() : price(0.0) {}
    InternalPriceLevel(double p) : price(p) {}

    InternalPriceLevel(const InternalPriceLevel& other)
        : price(other.price), total_quantity(other.total_quantity),
          first_order(nullptr), last_order(nullptr), order_count(other.order_count),
          is_active(other.is_active) {}

    InternalPriceLevel& operator=(const InternalPriceLevel& other) {
        if (this != &other) {
            price = other.price;
            total_quantity = other.total_quantity;
            first_order = nullptr;
            last_order = nullptr;
            order_count = other.order_count;
            is_active = other.is_active;
        }
        return *this;
    }

    void add_order(Order* order) {
        if (first_order == nullptr) {
            first_order = order;
            last_order = order;
        } else {
            order->prev = last_order;
            if (last_order) {
                last_order->next = order;
            }
            last_order = order;
        }

        total_quantity += order->quantity;
        order_count++;
    }

    void remove_order(Order* order) {
        if (!order->is_active) {
            return;
        }

        order->is_active = false;

        Order* prev = order->prev;
        Order* next = order->next;

        if (prev) {
            prev->next = next;
        } else {
            first_order = next;
        }

        if (next) {
            next->prev = prev;
        } else {
            last_order = prev;
        }

        total_quantity -= order->quantity;
        order_count--;
    }

    bool is_empty() const {
        return order_count == 0;
    }
};

// Implementation class using PIMPL idiom
class OrderBook::Impl {
public:
    std::map<Price, InternalPriceLevel*, std::greater<Price>> bids_;
    std::map<Price, InternalPriceLevel*> asks_;
    std::unordered_map<uint64_t, Order*> order_lookup_;
    SimpleMemoryPool<Order> order_pool_;
    SimpleMemoryPool<InternalPriceLevel> level_pool_;

    bool matching_in_progress_{false};
    uint64_t version_{0};

    ~Impl() {
        // Clean up orders
        for (auto& [id, order] : order_lookup_) {
            if (order) {
                order_pool_.deallocate(order);
            }
        }
        order_lookup_.clear();

        // Clean up bid levels
        for (auto& [price, level] : bids_) {
            if (level) {
                level_pool_.deallocate(level);
            }
        }
        bids_.clear();

        // Clean up ask levels
        for (auto& [price, level] : asks_) {
            if (level) {
                level_pool_.deallocate(level);
            }
        }
        asks_.clear();
    }

    InternalPriceLevel* get_or_create_level(Price price, bool is_buy) {
        if (is_buy) {
            auto it = bids_.find(price);
            if (it != bids_.end()) {
                return it->second;
            }

            InternalPriceLevel* level = level_pool_.allocate();
            *level = InternalPriceLevel(price);
            level->total_quantity = 0;
            level->order_count = 0;
            level->is_active = true;
            level->first_order = nullptr;
            level->last_order = nullptr;
            bids_[price] = level;
            return level;
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) {
                return it->second;
            }

            InternalPriceLevel* level = level_pool_.allocate();
            *level = InternalPriceLevel(price);
            level->total_quantity = 0;
            level->order_count = 0;
            level->is_active = true;
            level->first_order = nullptr;
            level->last_order = nullptr;
            asks_[price] = level;
            return level;
        }
    }

    InternalPriceLevel* get_level(Price price, bool is_buy) const {
        if (is_buy) {
            auto it = bids_.find(price);
            return (it != bids_.end()) ? it->second : nullptr;
        } else {
            auto it = asks_.find(price);
            return (it != asks_.end()) ? it->second : nullptr;
        }
    }

    void remove_price_level(Price price, bool is_buy) {
        if (is_buy) {
            auto it = bids_.find(price);
            if (it != bids_.end()) {
                level_pool_.deallocate(it->second);
                bids_.erase(it);
            }
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) {
                level_pool_.deallocate(it->second);
                asks_.erase(it);
            }
        }
    }

    void match_orders() {
        if (matching_in_progress_) {
            return;
        }

        matching_in_progress_ = true;

        bool matched = true;
        while (matched && !bids_.empty() && !asks_.empty()) {
            matched = false;

            auto best_bid = bids_.begin();
            auto best_ask = asks_.begin();

            if (best_bid->first < best_ask->first) {
                break;
            }

            InternalPriceLevel* bid_level = best_bid->second;
            InternalPriceLevel* ask_level = best_ask->second;

            if (!bid_level->is_active || !ask_level->is_active) {
                break;
            }

            Order* bid_order = bid_level->first_order;
            Order* ask_order = ask_level->first_order;

            if (!bid_order || !ask_order || !bid_order->is_active || !ask_order->is_active) {
                break;
            }

            uint64_t bid_qty = bid_order->quantity;
            uint64_t ask_qty = ask_order->quantity;
            uint64_t match_quantity = std::min(bid_qty, ask_qty);

            double match_price = (bid_order->timestamp_ns <= ask_order->timestamp_ns) 
                                ? bid_order->price : ask_order->price;

            std::cout << "MATCH: " << match_quantity << " @ " << match_price 
                      << " (Bid: " << bid_order->order_id << ", Ask: " << ask_order->order_id << ")\n";

            bid_order->quantity -= match_quantity;
            ask_order->quantity -= match_quantity;

            bool bid_removed = remove_filled_order(bid_order, bid_level, best_bid, true);
            bool ask_removed = remove_filled_order(ask_order, ask_level, best_ask, false);

            matched = true;

            if (bid_removed || ask_removed) {
                continue;
            }
        }

        matching_in_progress_ = false;
    }

    bool remove_filled_order(Order* order, InternalPriceLevel* level, 
                           const std::map<Price, InternalPriceLevel*>::iterator& map_it, bool is_buy) {
        if (order->quantity == 0) {
            level->remove_order(order);

            order_lookup_.erase(order->order_id);
            order_pool_.deallocate(order);

            if (level->is_empty()) {
                if (is_buy) {
                    bids_.erase(map_it);
                } else {
                    asks_.erase(map_it);
                }
                level_pool_.deallocate(level);
                return true;
            }
        }
        return false;
    }
};

// OrderBook implementation
OrderBook::OrderBook() : pImpl(std::make_unique<Impl>()) {}

OrderBook::~OrderBook() = default;

void OrderBook::add_order(const Order& o) {
    if (o.order_id == 0) {
        std::cerr << "Error: Invalid order ID (0)\n";
        return;
    }

    if (o.price < MIN_PRICE || o.price > MAX_PRICE || std::isnan(o.price) || std::isinf(o.price)) {
        std::cerr << "Error: Invalid price: " << o.price << " (must be between " << MIN_PRICE << " and " << MAX_PRICE << ")\n";
        return;
    }

    if (o.quantity == 0 || o.quantity > MAX_ORDER_QUANTITY) {
        std::cerr << "Error: Invalid quantity: " << o.quantity << " (must be between 1 and " << MAX_ORDER_QUANTITY << ")\n";
        return;
    }

    if (pImpl->order_lookup_.find(o.order_id) != pImpl->order_lookup_.end()) {
        std::cerr << "Error: Duplicate order ID: " << o.order_id << "\n";
        return;
    }

    Order* new_order = pImpl->order_pool_.allocate();
    if (!new_order) {
        std::cerr << "Error: Failed to allocate memory for order\n";
        return;
    }

    *new_order = o;
    new_order->next = nullptr;
    new_order->prev = nullptr;
    new_order->is_active = true;

    pImpl->order_lookup_[o.order_id] = new_order;

    InternalPriceLevel* level = pImpl->get_or_create_level(o.price, o.is_buy);
    if (!level) {
        pImpl->order_lookup_.erase(o.order_id);
        pImpl->order_pool_.deallocate(new_order);
        return;
    }

    level->add_order(new_order);
    pImpl->version_++;

    pImpl->match_orders();
}

bool OrderBook::cancel_order(uint64_t id) {
    if (id == 0) {
        std::cerr << "Error: Invalid order ID (0)\n";
        return false;
    }

    auto it = pImpl->order_lookup_.find(id);
    if (it == pImpl->order_lookup_.end()) {
        std::cerr << "Error: Order not found: " << id << "\n";
        return false;
    }

    Order* order = it->second;
    pImpl->order_lookup_.erase(it);

    if (!order || !order->is_active) {
        if (order) {
            pImpl->order_pool_.deallocate(order);
        }
        return false;
    }

    InternalPriceLevel* level = pImpl->get_level(order->price, order->is_buy);

    if (level) {
        level->remove_order(order);

        if (level->is_empty()) {
            pImpl->remove_price_level(level->price, order->is_buy);
        }
    }

    pImpl->order_pool_.deallocate(order);
    pImpl->version_++;
    return true;
}

bool OrderBook::amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) {
    if (order_id == 0) {
        std::cerr << "Error: Invalid order ID (0)\n";
        return false;
    }

    if (new_price < MIN_PRICE || new_price > MAX_PRICE || std::isnan(new_price) || std::isinf(new_price)) {
        std::cerr << "Error: Invalid price: " << new_price << " (must be between " << MIN_PRICE << " and " << MAX_PRICE << ")\n";
        return false;
    }

    if (new_quantity == 0 || new_quantity > MAX_ORDER_QUANTITY) {
        std::cerr << "Error: Invalid quantity: " << new_quantity << " (must be between 1 and " << MAX_ORDER_QUANTITY << ")\n";
        return false;
    }

    auto it = pImpl->order_lookup_.find(order_id);
    if (it == pImpl->order_lookup_.end()) {
        std::cerr << "Error: Order not found: " << order_id << "\n";
        return false;
    }

    Order* order = it->second;
    if (!order || !order->is_active) {
        std::cerr << "Error: Order is not active: " << order_id << "\n";
        return false;
    }

    if (order->price != new_price) {
        // Price change - treat as cancel + add
        InternalPriceLevel* old_level = pImpl->get_level(order->price, order->is_buy);
        if (old_level) {
            old_level->remove_order(order);
            if (old_level->is_empty()) {
                pImpl->remove_price_level(old_level->price, order->is_buy);
            }
        }

        order->price = new_price;
        order->quantity = new_quantity;

        InternalPriceLevel* new_level = pImpl->get_or_create_level(new_price, order->is_buy);
        if (!new_level) {
            std::cerr << "Error: Failed to create price level for " << new_price << "\n";
            return false;
        }
        new_level->add_order(order);
    } else {
        // Only quantity change - update in place
        InternalPriceLevel* level = pImpl->get_level(order->price, order->is_buy);
        if (level) {
            level->total_quantity = level->total_quantity - order->quantity + new_quantity;
            order->quantity = new_quantity;
        } else {
            std::cerr << "Error: Price level not found for order " << order_id << "\n";
            return false;
        }
    }

    pImpl->version_++;
    return true;
}

void OrderBook::get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const {
    bids.clear();
    asks.clear();

    auto bid_it = pImpl->bids_.begin();
    for (size_t i = 0; i < depth && bid_it != pImpl->bids_.end(); ++i, ++bid_it) {
        PriceLevel level;
        level.price = bid_it->second->price;
        level.total_quantity = bid_it->second->total_quantity;
        bids.push_back(level);
    }

    auto ask_it = pImpl->asks_.begin();
    for (size_t i = 0; i < depth && ask_it != pImpl->asks_.end(); ++i, ++ask_it) {
        PriceLevel level;
        level.price = ask_it->second->price;
        level.total_quantity = ask_it->second->total_quantity;
        asks.push_back(level);
    }
}

void OrderBook::print_book(size_t depth) const {
    std::cout << "\n=== ORDER BOOK ===\n";
    std::cout << "Bids (Buy)          | Asks (Sell)\n";
    std::cout << "Price    | Quantity | Price    | Quantity\n";
    std::cout << "---------|----------|----------|----------\n";

    auto bid_it = pImpl->bids_.begin();
    auto ask_it = pImpl->asks_.begin();

    for (size_t i = 0; i < depth && (bid_it != pImpl->bids_.end() || ask_it != pImpl->asks_.end()); ++i) {
        std::cout << std::fixed << std::setprecision(2);

        if (bid_it != pImpl->bids_.end()) {
            double price = bid_it->second->price;
            uint64_t qty = bid_it->second->total_quantity;
            std::cout << std::setw(8) << price << " | " << std::setw(8) << qty;
            ++bid_it;
        } else {
            std::cout << "         |          ";
        }

        std::cout << " | ";

        if (ask_it != pImpl->asks_.end()) {
            double price = ask_it->second->price;
            uint64_t qty = ask_it->second->total_quantity;
            std::cout << std::setw(8) << price << " | " << std::setw(8) << qty;
            ++ask_it;
        } else {
            std::cout << "         |          ";
        }

        std::cout << "\n";
    }

    std::cout << "\nBest Bid: " << get_best_bid() << "\n";
    std::cout << "Best Ask: " << get_best_ask() << "\n";
    std::cout << "Spread: " << get_spread() << "\n";
}

double OrderBook::get_best_bid() const {
    if (pImpl->bids_.empty()) return 0.0;
    return pImpl->bids_.begin()->second->price;
}

double OrderBook::get_best_ask() const {
    if (pImpl->asks_.empty()) return std::numeric_limits<double>::max();
    return pImpl->asks_.begin()->second->price;
}

double OrderBook::get_spread() const {
    double best_ask_price = get_best_ask();
    return best_ask_price == std::numeric_limits<double>::max() ? 0.0 : best_ask_price - get_best_bid();
}

uint64_t OrderBook::get_version() const {
    return pImpl->version_;
}

size_t OrderBook::get_order_count() const {
    return pImpl->order_lookup_.size();
}

size_t OrderBook::get_bid_levels() const {
    return pImpl->bids_.size();
}

size_t OrderBook::get_ask_levels() const {
    return pImpl->asks_.size();
}
