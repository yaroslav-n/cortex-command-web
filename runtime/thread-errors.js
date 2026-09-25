// Linked with --pre-js, so it runs on the page and in every pthread worker.
//
// A JavaScript error that kills a worker (a stack overflow, for instance) reaches the
// page only as Emscripten's one line, "worker sent an error! cortex.js:1: Uncaught
// RangeError: …", without the stack that would say where. The worker prints the stack
// itself before that happens: in a worker, err() posts to the page's printErr, so the
// stack lands in the page's log and on its failure screen.
if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope) {
  // A stack overflow's own stack is the recursion; this many frames show its cycle.
  Error.stackTraceLimit = 40;
  self.addEventListener('error', (event) => {
    const stack = event.error && event.error.stack;
    if (stack) err('Worker error: ' + stack);
  });
}
