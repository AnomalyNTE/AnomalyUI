#pragma once

#include <chrono>
#include <functional>

namespace anomaly {

// The only game-side edge the platform UI depends on: the tick that drives
// plugin Update callbacks and the management surface's own maintenance.
//
// In the Anomaly runtime this is implemented by the UE5/NTE adapter, which
// validates the game thread and the active Profile before handing a tick over.
// AnomalyUI keeps the seam as an interface so the launcher, the overlay preview
// and any other host can supply their own source -- or none at all, in which
// case the standalone host drives GameUpdate from its own render loop.
class GameTickSource {
public:
    using TickCallback = std::function<void(double)>;

    GameTickSource() = default;
    GameTickSource(const GameTickSource&) = delete;
    GameTickSource& operator=(const GameTickSource&) = delete;
    virtual ~GameTickSource() = default;

    // Replaces the current callback. The host installs one that pumps plugin
    // update work; a tick may already be in flight when this returns.
    virtual void SetTickCallback(TickCallback callback) = 0;

    // Detaches the callback immediately, then waits for an already-entered tick
    // until the deadline. A false result means a tick is still in flight; the
    // callback is nevertheless detached and will not be entered again.
    virtual bool ClearTickCallback(
        std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) noexcept = 0;
};

}  // namespace anomaly
