import { deflateSync } from 'node:zlib';

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, body) {
  const name = Buffer.from(type);
  const length = Buffer.alloc(4);
  const crc = Buffer.alloc(4);
  length.writeUInt32BE(body.length);
  crc.writeUInt32BE(crc32(Buffer.concat([name, body])));
  return Buffer.concat([length, name, body, crc]);
}
function segment(x, y, ax, ay, bx, by) {
  const dx = bx - ax, dy = by - ay;
  const factor = Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)));
  return Math.hypot(x - ax - factor * dx, y - ay - factor * dy);
}
export function createIcon(size) {
  const rows = Buffer.alloc((size * 4 + 1) * size);
  const lines = [[114, 374, 114, 116], [114, 116, 398, 116], [398, 116, 398, 374], [140, 349, 225, 381], [372, 349, 287, 381]];
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const px = (x + .5) * 512 / size, py = (y + .5) * 512 / size;
    let color = [16, 26, 45];
    if (lines.some(line => segment(px, py, ...line) < 15)) color = [112, 188, 255];
    if (Math.hypot(px - 257, py - 218) < 51) color = [229, 239, 255];
    if (Math.abs(px - 257) + Math.abs(py - 79) < 26) color = [255, 220, 105];
    const index = y * (size * 4 + 1) + 1 + x * 4;
    rows.set([...color, 255], index);
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(size, 0); header.writeUInt32BE(size, 4);
  header[8] = 8; header[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', header), chunk('IDAT', deflateSync(rows)), chunk('IEND', Buffer.alloc(0))]);
}
