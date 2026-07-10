#pragma once

struct weather_data;
struct room_data;

// Base class that encapsulates the singleton implementation
template <class T>
class singleton {
public:
    static void create()
    {
        static T theInstance;
        m_pInstance = &theInstance;
    }

    static T& instance()
    {
        if (!m_pInstance) {
            if (m_bDestroyed) {
                on_instance_destroyed();
            } else {
                on_instance_not_created();
            }
        }
        return *m_pInstance;
    }

protected:
    virtual ~singleton() { }

    // Empty stubs with no overriders anywhere in the codebase (see AGENTS.md: this
    // whole class template is unused/dead code) — plain static functions, not virtual
    // dispatch, since instance() (also static) has no object to dispatch through.
    static void on_instance_destroyed() {};
    static void on_instance_not_created() {};

private:
    // Deleted functions.
    // Don't put definitions in here so trying to do them will cause a compile error.
    // C++20 no longer accepts a constructor/operator= declared with the
    // template-id form (ClassName<T>(...)) here -- only the injected-class-
    // name (ClassName(...)) is a valid constructor-name; GCC's older,
    // permissive-extension acceptance of the template-id spelling is gone
    // under strict -std=c++20 (Phase 4 Wave 1 Task 1). Purely a spelling fix:
    // these declarations are still private and never defined, so copying a
    // singleton<T> is still a compile error either way.
    singleton(const singleton& other);
    singleton& operator=(const singleton&);

    static T* m_pInstance;
    static bool m_bDestroyed;
};

// Base class that encapsulates the singleton implementation.
// This class allows for weather_data and room_data in the singleton.
template <class T>
class world_singleton {
public:
    static void create(const weather_data& weather, const room_data* world)
    {
        static T theInstance(&weather, world);
        m_pInstance = &theInstance;
    }

    static T& instance()
    {
        if (!m_pInstance) {
            if (m_bDestroyed) {
                // on_instance_destroyed();
            } else {
                // on_instance_not_created();
            }
        }
        return *m_pInstance;
    }

protected:
    world_singleton()
        : m_weather(0)
        , m_world(0)
    {
    }
    world_singleton(const weather_data* weather, const room_data* world)
        : m_weather(weather)
        , m_world(world)
    {
    }

    virtual ~world_singleton() { }

    const weather_data& get_weather() const { return *m_weather; }
    const room_data* get_world() const { return m_world; }

    virtual void on_instance_destroyed() {};
    virtual void on_instance_not_created() {};

private:
    // Deleted functions.
    // Don't put definitions in here so trying to do them will cause a compile error.
    // Same C++20 template-id-as-constructor-name fix as singleton<T> above.
    world_singleton(const world_singleton& other);
    world_singleton& operator=(const world_singleton&);

    static T* m_pInstance;
    static bool m_bDestroyed;

    const weather_data* m_weather;
    const room_data* m_world;
};
