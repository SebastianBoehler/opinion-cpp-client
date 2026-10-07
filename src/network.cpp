#include "opinion/network.hpp"

#include <mutex>

namespace opinion
{
    namespace
    {
        std::mutex &route_mutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        NetworkRoute &route_slot()
        {
            static NetworkRoute route;
            return route;
        }
    } // namespace

    void set_default_network_route(const NetworkRoute &route)
    {
        std::lock_guard<std::mutex> lock(route_mutex());
        route_slot() = route;
    }

    NetworkRoute default_network_route()
    {
        std::lock_guard<std::mutex> lock(route_mutex());
        return route_slot();
    }
} // namespace opinion
