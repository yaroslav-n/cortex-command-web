#include "ThreadMan.h"

using namespace RTE;

ThreadMan::ThreadMan() {
#ifndef __EMSCRIPTEN__
	Clear();
#endif
	// Browser pools are already constructed with their final 4/2 worker counts.
	// Resetting them here needlessly joins workers on the browser main thread.
	Create();
}

ThreadMan::~ThreadMan() {
	Destroy();
}

void ThreadMan::Clear() {
#ifdef __EMSCRIPTEN__
	m_PriorityThreadPool.reset(4);
	m_BackgroundThreadPool.reset(2);
#else
	m_PriorityThreadPool.reset();
	m_BackgroundThreadPool.reset(std::thread::hardware_concurrency() / 2);
#endif
}

int ThreadMan::Create() {
	return 0;
}

void ThreadMan::Destroy() {
	Clear();
}
