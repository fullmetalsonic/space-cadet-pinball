export function canvasCssSize(width, height) {
  if (![width, height].every(value => Number.isFinite(value) && value > 0)) return null;
  return { width: Math.max(1, Math.floor(width)), height: Math.max(1, Math.floor(height)) };
}

// SDL's Emscripten window driver reads CSS dimensions on the normal resize event.
// Never change the canvas backing attributes separately from the SDL window.
export function createCanvasResizer({ stageSize, bufferSize, applyCssSize, notifyResize, schedule }) {
  let ready = false;
  let scheduled = false;
  let applied = null;
  const targetSize = () => {
    const size = stageSize();
    return canvasCssSize(size.width, size.height);
  };
  function apply(size) {
    if (!applied || applied.width !== size.width || applied.height !== size.height) {
      applyCssSize(size);
      applied = size;
    }
  }
  function needsResize(size) {
    const buffer = bufferSize();
    return buffer.width !== size.width || buffer.height !== size.height;
  }
  function update() {
    const size = targetSize();
    if (!size) return;
    apply(size);
    if (!ready || scheduled || !needsResize(size)) return;
    scheduled = true;
    schedule(() => {
      scheduled = false;
      const latest = targetSize();
      if (!latest) return;
      apply(latest);
      if (ready && needsResize(latest)) notifyResize();
    });
  }
  return { update, markReady() { ready = true; update(); } };
}
