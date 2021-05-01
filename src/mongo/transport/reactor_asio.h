#pragma once

#include "mongo/util/net/ssl_options.h"
#include "mongo/transport/asio_utils.h"
#include "mongo/transport/transport_layer.h"
#include "mongo/transport/baton.h"
// #include "mongo/logv2/log.h"

#include <asio.hpp>

namespace mongo {
namespace transport {

class ASIOReactor final : public Reactor {
public:
    ASIOReactor() : _ioContext() {}

    void run() noexcept override {
        ThreadIdGuard threadIdGuard(this);
        asio::io_context::work work(_ioContext);
        _ioContext.run();
    }

    void runFor(Milliseconds time) noexcept override {
        ThreadIdGuard threadIdGuard(this);
        asio::io_context::work work(_ioContext);
        _ioContext.run_for(time.toSystemDuration());
    }

    void stop() override {
        _ioContext.stop();
    }

    void drain() override {
        ThreadIdGuard threadIdGuard(this);
        _ioContext.restart();
        while (_ioContext.poll()) {
            // LOGV2_DEBUG(23012124214325346, 2, "Draining remaining work in reactor.");
        }
        _ioContext.stop();
    }

    std::unique_ptr<ReactorTimer> makeTimer() override {
        return std::make_unique<ASIOReactorTimer>(_ioContext);
    }

    Date_t now() override {
        return Date_t(asio::system_timer::clock_type::now());
    }

    void schedule(Task task) override {
        asio::post(_ioContext, [task = std::move(task)] { task(Status::OK()); });
    }

    void dispatch(Task task) override {
        asio::dispatch(_ioContext, [task = std::move(task)] { task(Status::OK()); });
    }

    bool onReactorThread() const override {
        return this == _reactorForThread;
    }

    operator asio::io_context&() {
        return _ioContext;
    }

private:
    class ThreadIdGuard {
    public:
        ThreadIdGuard(ASIOReactor* reactor) {
            invariant(!_reactorForThread);
            _reactorForThread = reactor;
        }

        ~ThreadIdGuard() {
            invariant(_reactorForThread);
            _reactorForThread = nullptr;
        }
    };

    class ASIOReactorTimer final : public ReactorTimer {
    public:
        explicit ASIOReactorTimer(asio::io_context& ctx)
            : _timer(std::make_shared<asio::system_timer>(ctx)) {}

        ~ASIOReactorTimer() {
            // The underlying timer won't get destroyed until the last promise from _asyncWait
            // has been filled, so cancel the timer so our promises get fulfilled
            cancel();
        }

        void cancel(const BatonHandle& baton = nullptr) override {
            // If we have a baton try to cancel that.
            if (baton && baton->networking() && baton->networking()->cancelTimer(*this)) {
                // LOGV2_DEBUG(2301044576586485, 2, "Canceled via baton, skipping asio cancel.");
                return;
            }

            // Otherwise there could be a previous timer that was scheduled normally.
            _timer->cancel();
        }

        Future<void> waitUntil(Date_t expiration, const BatonHandle& baton = nullptr) override {
            if (baton && baton->networking()) {
                return _asyncWait([&] { return baton->networking()->waitUntil(*this, expiration); },
                                  baton);
            } else {
                return _asyncWait([&] { _timer->expires_at(expiration.toSystemTimePoint()); });
            }
        }

    private:
        template <typename ArmTimerCb>
        Future<void> _asyncWait(ArmTimerCb&& armTimer) {
            try {
                cancel();

                armTimer();
                return _timer->async_wait(UseFuture{}).tapError([timer = _timer](const Status& status) {
                    if (status != ErrorCodes::CallbackCanceled) {
                        // LOGV2_DEBUG(2301123684523642, 2, "Timer received error: {status}", "status"_attr = status);
                    }
                });

            } catch (asio::system_error& ex) {
                return Future<void>::makeReady(errorCodeToStatus(ex.code()));
            }
        }

        template <typename ArmTimerCb>
        Future<void> _asyncWait(ArmTimerCb&& armTimer, const BatonHandle& baton) {
            cancel(baton);

            auto pf = makePromiseFuture<void>();
            armTimer().getAsync([p = std::move(pf.promise)](Status status) mutable {
                if (status.isOK()) {
                    p.emplaceValue();
                } else {
                    p.setError(status);
                }
            });

            return std::move(pf.future);
        }

        std::shared_ptr<asio::system_timer> _timer;
    };

    static thread_local ASIOReactor* _reactorForThread;

    asio::io_context _ioContext;
};

} // namespace transport
} // namespace mongo
