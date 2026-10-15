import test from 'node:test';
import assert from 'node:assert/strict';
import { boundedName, createNameInput } from '../name-input.mjs';

class Target {
  listeners = new Map(); style = {}; hidden = false; value = '';
  selectionStart = 0; selectionEnd = 0; selectionDirection = 'forward';
  addEventListener(type, callback) { const list = this.listeners.get(type) ?? []; list.push(callback); this.listeners.set(type, list); }
  fire(type, values = {}) {
    const event = { target: this, preventDefault() { this.prevented = true; }, stopImmediatePropagation() { this.stopped = true; }, ...values };
    for (const callback of this.listeners.get(type) ?? []) callback(event);
    return event;
  }
  setAttribute(name, value) { this[name] = value; }
  setSelectionRange(start, end) { this.selectionStart = start; this.selectionEnd = end; }
}
function harness() {
  const document = { activeElement: null, body: { append() {} }, createElement() { return input; } };
  const input = new Target();
  let focusCount = 0;
  input.focus = () => { focusCount++; document.activeElement = input; };
  input.blur = () => { if (document.activeElement === input) { document.activeElement = null; input.fire('blur'); } };
  const window = new Target(); window.visualViewport = new Target();
  const edits = [], submissions = [];
  const bridge = createNameInput({ document, window, canvas: { getBoundingClientRect: () => ({ left: 5, top: 20, width: 400, height: 600 }) },
    edit: (...args) => edits.push(args), submit: session => submissions.push(session), activateAudio() {} });
  const show = (session = 1, value = '비행사 1') => bridge.update(session, value, '이름', 50, 100, 200, 30, 400, 600);
  const enter = values => window.fire('keydown', { target: input, key: 'Enter', keyCode: 13, ...values });
  const change = (value, start = value.length, end = start) => { input.value = value; input.setSelectionRange(start, end); input.fire('input'); };
  return { document, input, window, bridge, edits, submissions, show, enter, change, focusCount: () => focusCount };
}

test('UTF-8 byte budget preserves complete Korean and supplementary characters', () => {
  assert.equal(boundedName('한'.repeat(11)), '한'.repeat(10));
  assert.equal(boundedName('😀'.repeat(8)), '😀'.repeat(7));
  assert.equal(boundedName('abc\n\0한'), 'abc한');
});
test('Korean IME commits once and its terminating Enter does not submit', () => {
  const h = harness(); h.show(); h.input.focus();
  h.input.fire('compositionstart');
  h.input.value = '한글'; h.input.setSelectionRange(2, 2);
  h.input.fire('input', { isComposing: true });
  assert.equal(h.edits.length, 0);
  assert.equal(h.enter({ isComposing: true }).prevented, undefined);
  assert.deepEqual(h.submissions, []);
  h.input.fire('compositionend'); h.input.fire('input', { isComposing: false });
  assert.deepEqual(h.edits, [[1, '한글', 6, 6]]);
  const enter = h.enter();
  assert.equal(enter.prevented, true); assert.equal(enter.stopped, true);
  assert.deepEqual(h.submissions, [1]); assert.equal(h.edits.length, 1);
});
test('deletion and selected-range replacement synchronize full value and UTF-8 cursor', () => {
  const h = harness(); h.show(); h.input.focus();
  h.change('가나다', 1, 2);
  h.change('가Z다', 2);
  h.change('가Z', 2);
  assert.deepEqual(h.edits, [[1, '가나다', 6, 3], [1, '가Z다', 4, 4], [1, '가Z', 4, 4]]);
});
test('frame publication never opens keyboard, preserves composition and repositions after resize', () => {
  const h = harness(); h.show();
  assert.equal(h.focusCount(), 0); assert.equal(h.input.style.left, '55px');
  h.input.focus(); h.input.fire('compositionstart'); h.input.value = '조합';
  h.show(1, '비행사 1'); assert.equal(h.input.value, '조합');
  h.window.visualViewport.fire('resize'); assert.equal(h.input.style.top, '120px');
  h.bridge.suspend(); assert.equal(h.input.value, '비행사 1');
  h.show(); assert.equal(h.focusCount(), 1); assert.equal(h.document.activeElement, null);
});
test('dialog closure invalidates pending browser events and next session restores new default', () => {
  const h = harness(); h.show(); h.input.focus(); h.change('검증');
  h.bridge.update(0); h.change('늦은 이벤트'); h.enter();
  assert.equal(h.edits.length, 1); assert.deepEqual(h.submissions, []);
  h.show(2, 'Player 2'); assert.equal(h.input.value, 'Player 2');
  assert.equal(h.input.hidden, false); assert.equal(h.document.activeElement, null);
});
