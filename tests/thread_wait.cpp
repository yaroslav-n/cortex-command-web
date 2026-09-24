// Waiting for pool work on the page's main thread, as the engine does every
// simulation update (script and AI states, sight rays) and during loading.
// Checked against the real BS::multi_future::wait and RTE::BrowserCooperativeYield.
#include "BS_thread_pool.hpp"
#include "System.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <emscripten.h>

using Clock = std::chrono::steady_clock;

static double MillisecondsSince(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

static int Run() {
	BS::thread_pool pool(2);
	int yields = 0;
	double yieldMs = 0.0;

	// A wait shorter than a frame must not return to the event loop. Each return
	// used to cost a timer that Chrome clamps to 4 ms, and the engine waits like
	// this several times per update.
	std::atomic<int> quick{0};
	// Anything that returns to the event loop, however it does it, runs one of these.
	EM_ASM({
		Module.eventLoopRan = 0;
		setTimeout(() => Module.eventLoopRan = 1, 0);
		const channel = new MessageChannel();
		channel.port1.onmessage = () => Module.eventLoopRan = 1;
		channel.port2.postMessage(0);
	});
	RTE::BrowserNoteEventLoopTurn();
	RTE::BrowserTakeYieldStats(yields, yieldMs);
	const Clock::time_point quickStart = Clock::now();
	for (int round = 0; round < 20; ++round) {
		BS::multi_future<void> pending;
		for (int i = 0; i < 4; ++i) pending.push_back(pool.submit([&] { ++quick; }));
		pending.wait();
	}
	const double quickMs = MillisecondsSince(quickStart);
	RTE::BrowserTakeYieldStats(yields, yieldMs);
	const int eventLoopRan = EM_ASM_INT({ return Module.eventLoopRan; });
	if (quick != 80 || yields != 0 || eventLoopRan) {
		std::printf("FAIL short waits: %d of 80 tasks, %d cooperative yields, event loop %s, %.1f ms\n", int(quick), yields,
		            eventLoopRan ? "ran" : "did not run", quickMs);
		return 1;
	}

	// A long wait must still let the page run: timers fire while the main thread waits.
	EM_ASM({ Module.waitTicks = 0; Module.waitTimer = setInterval(() => ++Module.waitTicks, 2); });
	std::atomic<int> completed{0};
	BS::multi_future<void> pending;
	for (int i = 0; i < 4; ++i) pending.push_back(pool.submit([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		++completed;
	}));
	pending.wait();
	const int ticks = EM_ASM_INT({ clearInterval(Module.waitTimer); return Module.waitTicks; });
	RTE::BrowserTakeYieldStats(yields, yieldMs);
	if (completed != 4 || ticks < 1 || yields < 2) {
		std::printf("FAIL long wait: %d of 4 tasks, %d timer ticks, %d event loop returns\n", int(completed), ticks, yields);
		return 2;
	}

	// Work that calls back into the main thread (stdout is proxied there) must not
	// have to wait for an event loop return: the wait itself services those calls.
	RTE::BrowserNoteEventLoopTurn();
	const Clock::time_point proxiedStart = Clock::now();
	BS::multi_future<void> proxied;
	for (int i = 0; i < 4; ++i) proxied.push_back(pool.submit([i] { std::printf("worker line %d\n", i); }));
	proxied.wait();
	const double proxiedMs = MillisecondsSince(proxiedStart);
	if (proxiedMs > 12.0) {
		std::printf("FAIL proxied calls took %.1f ms to be serviced during a wait\n", proxiedMs);
		return 3;
	}

	BS::multi_future<void> empty;
	empty.wait();
	int deferredRan = 0;
	BS::multi_future<void> deferred;
	deferred.push_back(std::async(std::launch::deferred, [&] { ++deferredRan; }));
	deferred.wait();
	if (deferredRan != 1) return 4;
	BS::multi_future<void> exceptional;
	exceptional.push_back(pool.submit([] { throw std::runtime_error("expected"); }));
	exceptional.wait();
	try {
		exceptional[0].get();
		return 5;
	} catch (const std::runtime_error&) {}
	BS::multi_future<void> workerWait;
	workerWait.push_back(pool.submit([] {
		BS::multi_future<void> inner;
		inner.push_back(std::async(std::launch::deferred, [] {}));
		inner.wait();
	}));
	workerWait.wait();

	std::printf("PASS 20 short waits in %.1f ms with no event loop return; long wait let %d timer ticks through in %d returns; "
	            "proxied calls serviced in %.1f ms; empty, deferred, exception and worker-side waits\n",
	            quickMs, ticks, yields, proxiedMs);
	return 0;
}

// main's own return value is lost once it has suspended, so the
// page would report success either way; exit() carries the result.
int main(int, char**) {
	RTE::BrowserMarkEngineThread();
	std::exit(Run());
}
