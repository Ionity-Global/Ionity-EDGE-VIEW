/**
 * The floating chat crawl. Messages arrive from the server (Studio relays the
 * device message board and anything the AI announces) — the console never
 * invents them, it only scrolls them right to left.
 */
export class Crawl {
  constructor(lineEl, { speed = 42, gap = '   •   ', max = 40 } = {}) {
    this.el = lineEl;
    this.speed = speed;          // px per second
    this.gap = gap;
    this.max = max;
    this.queue = [];
    this.offset = 0;
    this.width = 1;
    this.paused = false;
    this._last = performance.now();
    this._render();
    requestAnimationFrame(t => this._tick(t));
  }

  push(text) {
    const clean = String(text).replace(/\s+/g, ' ').trim();
    if (!clean) return;
    this.queue.push(clean);
    while (this.queue.length > this.max) this.queue.shift();
    this._dirty = true;
    // Don't make the first real message wait out a full lap of the placeholder.
    if (this._placeholder) { this.offset = 0; this._render(); }
  }

  setPaused(p) { this.paused = p; }

  _render() {
    const body = this.queue.length ? this.queue.join(this.gap) : 'Waiting for the server…';
    this._placeholder = this.queue.length === 0;
    this.el.textContent = body + this.gap;
    this.width = this.el.scrollWidth || 1;
    this._dirty = false;
  }

  _tick(now) {
    const dt = Math.min(0.1, (now - this._last) / 1000);
    this._last = now;

    if (!this.paused) {
      this.offset += this.speed * dt;
      if (this.offset >= this.width) {
        this.offset = 0;
        if (this._dirty) this._render();   // swap in new messages only at the seam
      }
    }

    const track = this.el.parentElement;
    const start = track ? track.clientWidth : 0;
    this.el.style.transform = `translateX(${(start - this.offset).toFixed(1)}px)`;
    requestAnimationFrame(t => this._tick(t));
  }
}
