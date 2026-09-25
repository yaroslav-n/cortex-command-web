// Linked with --pre-js, so it runs on the page and in every pthread worker.
//
// When the game runs out of memory, the allocation that fails is whichever comes
// last, often on another thread than the one that used the memory up. So once the
// heap passes 2 GB, and at every further 512 MB, the thread that grows it prints the
// stack it was allocating from, and a failure report says what was growing. The heap
// grows on the thread whose allocation needs the room (sbrk calls
// emscripten_resize_heap there), which is why the stack names the allocation.
{
  const grow = WebAssembly.Memory.prototype.grow;
  const megabyte = 1024 * 1024;
  let nextReport = 2048 * megabyte;
  WebAssembly.Memory.prototype.grow = function (delta) {
    const result = grow.call(this, delta);
    const size = this.buffer.byteLength;
    if (size >= nextReport) {
      nextReport = (Math.floor(size / (512 * megabyte)) + 1) * 512 * megabyte;
      const limit = Error.stackTraceLimit;
      Error.stackTraceLimit = 40;
      const stack = new Error().stack.split('\n').slice(2).join('\n');
      Error.stackTraceLimit = limit;
      (typeof err === 'function' ? err : console.error)(`Heap grew to ${Math.round(size / megabyte)} MB of 4096 MB, allocating in:\n${stack}`);
    }
    return result;
  };
}
