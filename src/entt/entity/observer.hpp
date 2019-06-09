#ifndef ENTT_ENTITY_OBSERVER_HPP
#define ENTT_ENTITY_OBSERVER_HPP


#include <limits>
#include <cstddef>
#include <cstdint>
#include <utility>
#include "../config/config.h"
#include "../core/type_traits.hpp"
#include "registry.hpp"
#include "storage.hpp"
#include "entity.hpp"
#include "fwd.hpp"


namespace entt {


/**
 * @brief Matcher.
 *
 * Primary template isn't defined on purpose. All the specializations give a
 * compile-time error, but for a few reasonable cases.
 */
template<typename...>
struct matcher;


/**
 * @brief Observing matcher.
 *
 * An observing matcher contains a type for which changes should be
 * detected.<br/>
 * Because of the rules of the language, not all changes can be easily detected.
 * In order to avoid nasty solutions that could affect performance to an extent,
 * the matcher listens only to the `on_replace` signals emitted by a registry
 * and is therefore triggered whenever an instance of the given component is
 * explicitly replaced.
 *
 * @tparam AnyOf Type of component for which changes should be detected.
 */
template<typename AnyOf>
struct matcher<AnyOf> {};


/**
 * @brief Grouping matcher.
 *
 * A grouping matcher describes the group to track in terms of accepted and
 * excluded types.<br/>
 * This kind of matcher is triggered whenever an entity _enters_ the desired
 * group because of the components it is assigned.
 *
 * @tparam AllOf Types of components tracked by the matcher.
 * @tparam NoneOf Types of components used to filter out entities.
 */
template<typename... AllOf, typename... NoneOf>
struct matcher<type_list<AllOf...>, type_list<NoneOf...>> {};


/**
 * @brief Collector.
 *
 * A collector contains a set of rules (literally, matchers) to use to track
 * entities.<br/>
 * Its main purpose is to generate a descriptor that allows an observer to know
 * how to connect to a registry.
 *
 * @tparam AnyOf Types of components for which changes should be detected.
 * @tparam Matcher Types of grouping matchers.
 */
template<typename... Matcher>
struct basic_collector {
    /**
     * @brief Adds a grouping matcher to the collector.
     * @tparam AllOf Types of components tracked by the matcher.
     * @tparam NoneOf Types of components used to filter out entities.
     * @return The updated collector.
     */
    template<typename... AllOf, typename... NoneOf>
    static constexpr auto group(exclude_t<NoneOf...> = {}) ENTT_NOEXCEPT {
        return basic_collector<Matcher..., matcher<type_list<AllOf...>, type_list<NoneOf...>>>{};
    }

    /**
     * @brief Adds one or more observing matchers to the collector.
     * @tparam AnyOf Types of components for which changes should be detected.
     * @return The updated collector.
     */
    template<typename... AnyOf>
    static constexpr auto replace() ENTT_NOEXCEPT {
        return basic_collector<Matcher..., matcher<AnyOf>...>{};
    }
};


/*! @brief Variable template used to ease the definition of collectors. */
constexpr basic_collector<> collector{};


/**
 * @brief Observer.
 *
 * An observer returns all the entities and only the entities that fit the
 * requirements of at least one matcher. Moreover, it's guaranteed that the
 * entity list is tightly packed in memory for fast iterations.<br/>
 * In general, observers don't stay true to the order of any set of components.
 *
 * Observers work mainly with two types of matchers, provided through a
 * collector:
 *
 * * Observing matcher: an observer will return at least all the living entities
 *   for which one or more of the given components have been explicitly
 *   replaced and not yet destroyed.
 * * Grouping matcher: an observer will return at least all the living entities
 *   that would have entered the given group if it existed and that would have
 *   not yet left it.
 *
 * If an entity respects the requirements of multiple matchers, it will be
 * returned once and only once by the observer in any case.
 *
 * @b Important
 *
 * Iterators aren't invalidated if:
 *
 * * New instances of the given components are created and assigned to entities.
 * * The entity currently pointed is modified (as an example, if one of the
 *   given components is removed from the entity to which the iterator points).
 * * The entity currently pointed is destroyed.
 *
 * In all the other cases, modifying the pools of the given components in any
 * way invalidates all the iterators and using them results in undefined
 * behavior.
 *
 * @warning
 * Lifetime of an observer doesn't necessarily have to overcome the one of the
 * registry to which it is connected. However, the observer must be disconnected
 * from the registry before being destroyed to avoid crashes due to dangling
 * pointers.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 */
template<typename Entity>
class basic_observer {
    using traits_type = entt_traits<Entity>;
    using payload_type = std::uint32_t;

    template<std::size_t Index, typename>
    struct matcher_handler;

    template<std::size_t Index, typename AnyOf>
    struct matcher_handler<Index, matcher<AnyOf>> {
        static void maybe_valid_if(basic_observer *obs, const basic_registry<Entity> &, const Entity entt) {
            (obs->view.has(entt) ? obs->view.get(entt) : obs->view.construct(entt)) |= (1 << Index);
        }

        static void discard_if(basic_observer *obs, const basic_registry<Entity> &, const Entity entt) {
            if(auto *value = obs->view.try_get(entt); value && !(*value &= (~(1 << Index)))) {
                obs->view.destroy(entt);
            }
        }

        static void disconnect(basic_registry<Entity> &reg, const basic_observer &obs) {
            (reg.template on_replace<AnyOf>().disconnect(&obs));
            (reg.template on_destroy<AnyOf>().disconnect(&obs));
        }

        static void connect(basic_observer &obs, basic_registry<Entity> &reg) {
            reg.template on_replace<AnyOf>().template connect<&maybe_valid_if>(&obs);
            reg.template on_destroy<AnyOf>().template connect<&discard_if>(&obs);
        }
    };

    template<std::size_t Index, typename... AllOf, typename... NoneOf>
    struct matcher_handler<Index, matcher<type_list<AllOf...>, type_list<NoneOf...>>> {
        static void maybe_valid_if(basic_observer *obs, const basic_registry<Entity> &reg, const Entity entt) {
            if(reg.template has<AllOf...>(entt) && !(reg.template has<NoneOf>(entt) || ...)) {
                (obs->view.has(entt) ? obs->view.get(entt) : obs->view.construct(entt)) |= (1 << Index);
            }
        }

        static void discard_if(basic_observer *obs, const basic_registry<Entity> &, const Entity entt) {
            if(auto *value = obs->view.try_get(entt); value && !(*value &= (~(1 << Index)))) {
                obs->view.destroy(entt);
            }
        }

        static void disconnect(basic_registry<Entity> &reg, const basic_observer &obs) {
            ((reg.template on_construct<AllOf>().disconnect(&obs)), ...);
            ((reg.template on_destroy<AllOf>().disconnect(&obs)), ...);
            ((reg.template on_construct<NoneOf>().disconnect(&obs)), ...);
            ((reg.template on_destroy<NoneOf>().disconnect(&obs)), ...);
        }

        static void connect(basic_observer &obs, basic_registry<Entity> &reg) {
            (reg.template on_construct<AllOf>().template connect<&maybe_valid_if>(&obs), ...);
            (reg.template on_destroy<NoneOf>().template connect<&maybe_valid_if>(&obs), ...);
            (reg.template on_destroy<AllOf>().template connect<&discard_if>(&obs), ...);
            (reg.template on_construct<NoneOf>().template connect<&discard_if>(&obs), ...);
        }
    };

    template<auto... Disconnect>
    static void disconnect(basic_registry<Entity> &reg, const basic_observer &obs) {
        (Disconnect(reg, obs), ...);
    }

    template<typename... Matcher, std::size_t... Index>
    void connect(basic_registry<Entity> &reg, std::index_sequence<Index...>) {
        static_assert(sizeof...(Matcher) < std::numeric_limits<payload_type>::digits);
        release = &basic_observer::disconnect<&matcher_handler<Index, Matcher>::disconnect...>;
        (matcher_handler<Index, Matcher>::connect(*this, reg), ...);
    }

public:
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::entity_type;
    /*! @brief Unsigned integer type. */
    using size_type = typename sparse_set<Entity>::size_type;
    /*! @brief Input iterator type. */
    using iterator_type = typename sparse_set<Entity>::iterator_type;

    /*! @brief Default constructor. */
    basic_observer() ENTT_NOEXCEPT
        : target{}, release{}, view{}
    {}

    /*! @brief Default copy constructor, deleted on purpose. */
    basic_observer(const basic_observer &) = delete;
    /*! @brief Default move constructor, deleted on purpose. */
    basic_observer(basic_observer &&) = delete;

    /**
     * @brief Default copy assignment operator, deleted on purpose.
     * @return This observer.
     */
    basic_observer & operator=(const basic_observer &) = delete;

    /**
     * @brief Default move assignment operator, deleted on purpose.
     * @return This observer.
     */
    basic_observer & operator=(basic_observer &&) = delete;

    /**
     * @brief Creates an observer and connects it to a given registry.
     * @tparam Matcher Types of matchers to use to initialize the observer.
     * @param reg A valid reference to a registry.
     */
    template<typename... Matcher>
    basic_observer(basic_registry<entity_type> &reg, basic_collector<Matcher...>) ENTT_NOEXCEPT
        : target{&reg},
          release{},
          view{}
    {
        connect<Matcher...>(reg, std::make_index_sequence<sizeof...(Matcher)>{});
    }

    /*! @brief Default destructor. */
    ~basic_observer() = default;

    /**
     * @brief Connects an observer to a given registry.
     * @tparam Matcher Types of matchers to use to initialize the observer.
     * @param reg A valid reference to a registry.
     */
    template<typename... Matcher>
    void connect(basic_registry<entity_type> &reg, basic_collector<Matcher...>) {
        disconnect();
        connect<Matcher...>(reg, std::make_index_sequence<sizeof...(Matcher)>{});
        target = &reg;
        view.reset();
    }

    /*! @brief Disconnects an observer from the registry it keeps track of. */
    void disconnect() {
        if(release) {
            release(*target, *this);
            release = nullptr;
        }
    }

    /**
     * @brief Returns the number of elements in an observer.
     * @return Number of elements.
     */
    size_type size() const ENTT_NOEXCEPT {
        return view.size();
    }

    /**
     * @brief Checks whether an observer is empty.
     * @return True if the observer is empty, false otherwise.
     */
    bool empty() const ENTT_NOEXCEPT {
        return view.empty();
    }

    /**
     * @brief Direct access to the list of entities of the observer.
     *
     * The returned pointer is such that range `[data(), data() + size()]` is
     * always a valid range, even if the container is empty.
     *
     * @note
     * There are no guarantees on the order of the entities. Use `begin` and
     * `end` if you want to iterate the observer in the expected order.
     *
     * @return A pointer to the array of entities.
     */
    const entity_type * data() const ENTT_NOEXCEPT {
        return view.data();
    }

    /**
     * @brief Returns an iterator to the first entity of the observer.
     *
     * The returned iterator points to the first entity of the observer. If the
     * container is empty, the returned iterator will be equal to `end()`.
     *
     * @return An iterator to the first entity of the observer.
     */
    iterator_type begin() const ENTT_NOEXCEPT {
        return view.sparse_set<entity_type>::begin();
    }

    /**
     * @brief Returns an iterator that is past the last entity of the observer.
     *
     * The returned iterator points to the entity following the last entity of
     * the observer. Attempting to dereference the returned iterator results in
     * undefined behavior.
     *
     * @return An iterator to the entity following the last entity of the
     * observer.
     */
    iterator_type end() const ENTT_NOEXCEPT {
        return view.sparse_set<entity_type>::end();
    }

    /*! @brief Resets the underlying container. */
    void clear() {
        return view.reset();
    }

private:
    basic_registry<entity_type> *target;
    void(* release)(basic_registry<Entity> &, const basic_observer &);
    storage<entity_type, payload_type> view;
};


}


#endif // ENTT_ENTITY_OBSERVER_HPP
