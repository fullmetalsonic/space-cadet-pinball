const encoder = new TextEncoder();
export function boundedName(value) {
  let result = '', bytes = 0;
  for (const character of value.replace(/[\u0000-\u001f\u007f]/g, '')) {
    const size = encoder.encode(character).length;
    if (bytes + size > 31) break;
    result += character;
    bytes += size;
  }
  return result;
}

// The browser owns editing (including IME and selection); the existing game owns
// confirmation and persistence. No keyboard is opened by a render-frame callback.
export function createNameInput({ document, window, canvas, edit, submit, activateAudio, cancelInputs = () => {} }) {
  const input = document.createElement('input');
  input.id = 'high-score-name';
  input.type = 'text';
  input.hidden = true;
  input.autocomplete = 'off';
  input.spellcheck = false;
  input.enterKeyHint = 'done';
  input.setAttribute('autocapitalize', 'off');
  document.body.append(input);
  let session = 0, composing = false, compositionValue = '', lastEdit = '', geometry = null;
  const focused = () => document.activeElement === input;
  function commit() {
    if (!session || composing) return;
    const value = boundedName(input.value);
    const start = Math.min(input.selectionStart ?? value.length, value.length);
    const end = Math.min(input.selectionEnd ?? start, value.length);
    if (input.value !== value) { input.value = value; input.setSelectionRange(start, end); }
    const cursor = encoder.encode(value.slice(0, input.selectionDirection === 'backward' ? start : end)).length;
    const anchor = encoder.encode(value.slice(0, input.selectionDirection === 'backward' ? end : start)).length;
    const signature = JSON.stringify([session, value, cursor, anchor]);
    if (signature !== lastEdit) { edit(session, value, cursor, anchor); lastEdit = signature; }
  }
  input.addEventListener('pointerdown', () => activateAudio());
  input.addEventListener('focus', cancelInputs);
  input.addEventListener('compositionstart', () => { compositionValue = input.value; composing = true; });
  input.addEventListener('compositionend', () => { composing = false; commit(); });
  input.addEventListener('input', event => { if (!event.isComposing) commit(); });
  input.addEventListener('select', commit);
  input.addEventListener('blur', () => { if (!composing) commit(); });
  // SDL registers window keyboard listeners. Stop only propagation, so the
  // native input's editing, selection and IME defaults still run normally.
  for (const type of ['keydown', 'keyup', 'keypress']) window.addEventListener(type, event => {
    if (event.target !== input && !focused()) return;
    event.stopImmediatePropagation();
    if (type !== 'keydown' || !session) return;
    if (event.key === 'Enter' && !composing && !event.isComposing && event.keyCode !== 229) {
      event.preventDefault(); commit(); submit(session); input.blur();
    } else if (event.key === 'Escape' && !composing) { event.preventDefault(); input.blur(); }
  }, true);
  function position() {
    if (!geometry || !session) return;
    const rect = canvas.getBoundingClientRect();
    const [x, y, width, height, displayWidth, displayHeight] = geometry;
    if (displayWidth <= 0 || displayHeight <= 0) return;
    Object.assign(input.style, {
      left: `${rect.left + x * rect.width / displayWidth}px`,
      top: `${rect.top + y * rect.height / displayHeight}px`,
      width: `${width * rect.width / displayWidth}px`,
      height: `${height * rect.height / displayHeight}px`,
      fontSize: `${Math.max(12, height * rect.height / displayHeight * .65)}px`
    });
  }
  function hide() {
    session = 0; geometry = null; composing = false; lastEdit = '';
    input.hidden = true; input.blur();
  }
  function update(epoch, value, label, ...rect) {
    if (!epoch) { hide(); return; }
    if (session !== epoch) {
      hide(); session = epoch; input.value = value; input.setSelectionRange(0, value.length); lastEdit = '';
    } else if (!focused() && !composing && input.value !== value) input.value = value;
    input.setAttribute('aria-label', label);
    geometry = rect; input.hidden = false; position();
  }
  window.addEventListener('resize', position);
  window.visualViewport?.addEventListener('resize', position);
  window.visualViewport?.addEventListener('scroll', position);
  return { update, suspend() {
    if (composing) input.value = compositionValue;
    composing = false; input.blur();
  }, input };
}
