#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#ifndef __EMSCRIPTEN__
#include <execution>
#endif

#include "System.h"

#ifdef __EMSCRIPTEN__
#include "ThreadMan.h"
#include <emscripten.h>
#endif

namespace RTE {
#ifdef __EMSCRIPTEN__
	// Emscripten's libc++ has no parallel STL policies, so these loops ran
	// serially while the native build spread them over every core, which made
	// scene loading several times slower and blocked the page for the duration.
	// Dispatch to the engine's existing priority pool instead and wait
	// cooperatively, so the browser keeps painting and mixing audio meanwhile.
	template <class Iterator, class Function>
	void ForEach(Iterator first, Iterator last, Function function) {
		// Escape hatch for bisecting behaviour differences against the serial path.
		static const bool forceSerial = MAIN_THREAD_EM_ASM_INT({ return Module['serialLoops'] === true; });
		const auto count = std::distance(first, last);
		constexpr auto minimumParallelWork = 64;
		if (forceSerial) {
			std::for_each(first, last, function);
			return;
		}
		if (!System::PhaseRunsInParallel(System::ParallelAlgorithms)) {
			std::for_each(first, last, function);
			return;
		}
		const bool randomAccess = std::is_same<typename std::iterator_traits<Iterator>::iterator_category, std::random_access_iterator_tag>::value;
		// Only the engine thread may wait this way, and a pool thread must not
		// queue work onto the pool it is running on.
		if (!randomAccess || count < minimumParallelWork || !BrowserIsEngineThread()) {
			std::for_each(first, last, function);
			return;
		}

		auto futures = g_ThreadMan.GetPriorityThreadPool().parallelize_loop(
		    static_cast<std::size_t>(0), static_cast<std::size_t>(count),
		    [first, function](std::size_t blockStart, std::size_t blockEnd) {
			    for (std::size_t index = blockStart; index < blockEnd; ++index) {
				    function(*(first + static_cast<std::ptrdiff_t>(index)));
			    }
		    });

		for (std::size_t block = 0; block < futures.size(); ++block) {
			// A timed wait spins on this thread while servicing the calls workers
			// proxy to it; the yield only returns to the event loop after a frame.
			while (futures[block].wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
				BrowserCooperativeYield();
			}
		}
		futures.wait();
	}
#else
	template <class Iterator, class Function>
	void ForEach(Iterator first, Iterator last, Function function) {
		// A run that has to be reproducible takes the same serial path as the browser
		// build in that mode, so both do the work in the same order.
		if (!System::PhaseRunsInParallel(System::ParallelAlgorithms)) {
			std::for_each(first, last, function);
			return;
		}
		std::for_each(std::execution::par_unseq, first, last, function);
	}
#endif
} // namespace RTE
