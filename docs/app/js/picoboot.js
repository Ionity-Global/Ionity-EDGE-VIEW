/**
 * PICOBOOT over WebUSB — flashes a UF2 straight from the browser into an
 * RP2040/RP2350 sitting in BOOTSEL, plus a File System Access fallback that
 * simply copies the UF2 onto the RPI-RP2 mass-storage volume.
 *
 * Chrome/Edge only (WebUSB). On Windows the BOOTSEL device binds to WinUSB
 * already, so no driver swap is needed.
 */

const VENDOR_RP = 0x2e8a;
const PID_RP2040_BOOT = 0x0003;
const PID_RP2350_BOOT = 0x000f;

const CMD_MAGIC = 0x431fd10b;
const CMD_EXCLUSIVE_ACCESS = 0x01;
const CMD_REBOOT = 0x02;
const CMD_FLASH_ERASE = 0x03;
const CMD_WRITE = 0x05;
const CMD_EXIT_XIP = 0x06;

const FLASH_BASE = 0x10000000;
const SECTOR = 4096;
const CHUNK = 4096;

// UF2
const UF2_MAGIC0 = 0x0a324655;
const UF2_MAGIC1 = 0x9e5d5157;
const UF2_MAGIC_END = 0x0ab16f30;
const UF2_FLAG_NOT_MAIN_FLASH = 0x00000001;
const UF2_FLAG_FAMILY_PRESENT = 0x00002000;

export const usbSupported = typeof navigator !== 'undefined' && 'usb' in navigator;
export const driveSupported = typeof window !== 'undefined' && 'showDirectoryPicker' in window;
export const serialSupported = typeof navigator !== 'undefined' && 'serial' in navigator;

/** Split a UF2 into contiguous {addr, data} runs destined for flash. */
export function parseUf2(buffer) {
  const view = new DataView(buffer);
  const total = buffer.byteLength / 512;
  if (!Number.isInteger(total) || total === 0) throw new Error('not a UF2 (bad length)');

  const runs = [];
  let families = new Set();

  for (let i = 0; i < total; i++) {
    const o = i * 512;
    if (view.getUint32(o, true) !== UF2_MAGIC0 || view.getUint32(o + 4, true) !== UF2_MAGIC1)
      throw new Error(`not a UF2 (block ${i} magic)`);
    if (view.getUint32(o + 508, true) !== UF2_MAGIC_END)
      throw new Error(`not a UF2 (block ${i} end magic)`);

    const flags = view.getUint32(o + 8, true);
    if (flags & UF2_FLAG_NOT_MAIN_FLASH) continue;
    if (flags & UF2_FLAG_FAMILY_PRESENT) families.add(view.getUint32(o + 28, true));

    const addr = view.getUint32(o + 12, true);
    const size = view.getUint32(o + 16, true);
    if (size > 476) throw new Error(`block ${i} payload too large`);
    const data = new Uint8Array(buffer, o + 32, size);

    const last = runs[runs.length - 1];
    if (last && last.addr + last.length === addr) {
      last.parts.push(data);
      last.length += size;
    } else {
      runs.push({ addr, length: size, parts: [data] });
    }
  }

  for (const r of runs) {
    const flat = new Uint8Array(r.length);
    let p = 0;
    for (const part of r.parts) { flat.set(part, p); p += part.length; }
    r.data = flat;
    delete r.parts;
  }
  const bytes = runs.reduce((a, r) => a + r.length, 0);
  return { runs, bytes, families: [...families] };
}

class Picoboot {
  constructor(device, iface, epOut, epIn) {
    this.device = device;
    this.iface = iface;
    this.epOut = epOut;
    this.epIn = epIn;
    this.token = 1;
  }

  static async request() {
    if (!usbSupported) throw new Error('WebUSB is unavailable — use Chrome or Edge');
    const device = await navigator.usb.requestDevice({
      filters: [
        { vendorId: VENDOR_RP, productId: PID_RP2350_BOOT },
        { vendorId: VENDOR_RP, productId: PID_RP2040_BOOT },
      ],
    });
    return Picoboot.open(device);
  }

  static async open(device) {
    await device.open();
    if (!device.configuration) await device.selectConfiguration(1);

    // The PICOBOOT interface is the vendor-specific one (class 0xFF), not MSC.
    let found = null;
    for (const itf of device.configuration.interfaces) {
      for (const alt of itf.alternates) {
        if (alt.interfaceClass !== 0xff) continue;
        const out = alt.endpoints.find(e => e.direction === 'out' && e.type === 'bulk');
        const inp = alt.endpoints.find(e => e.direction === 'in' && e.type === 'bulk');
        if (out && inp) found = { num: itf.interfaceNumber, out: out.endpointNumber, in: inp.endpointNumber };
      }
    }
    if (!found) throw new Error('no PICOBOOT interface — is the board really in BOOTSEL?');

    await device.claimInterface(found.num);
    return new Picoboot(device, found.num, found.out, found.in);
  }

  async close() {
    try { await this.device.releaseInterface(this.iface); } catch { }
    try { await this.device.close(); } catch { }
  }

  /** 32-byte PICOBOOT command header. */
  buildCommand(id, argsFn, transferLength = 0, argSize = 0) {
    const buf = new ArrayBuffer(32);
    const v = new DataView(buf);
    v.setUint32(0, CMD_MAGIC, true);
    v.setUint32(4, this.token++, true);
    v.setUint8(8, id);
    v.setUint8(9, argSize);
    v.setUint16(10, 0, true);
    v.setUint32(12, transferLength, true);
    if (argsFn) argsFn(v);
    return new Uint8Array(buf);
  }

  async command(id, argsFn, argSize = 0, payload = null) {
    const len = payload ? payload.byteLength : 0;
    await this.device.transferOut(this.epOut, this.buildCommand(id, argsFn, len, argSize));
    if (payload) await this.device.transferOut(this.epOut, payload);
    // Zero-length ack on the opposite pipe.
    await this.device.transferIn(this.epIn, 1);
  }

  exclusiveAccess(mode = 1) {
    return this.command(CMD_EXCLUSIVE_ACCESS, v => v.setUint8(16, mode), 1);
  }

  exitXip() { return this.command(CMD_EXIT_XIP, null, 0); }

  erase(addr, size) {
    return this.command(CMD_FLASH_ERASE, v => { v.setUint32(16, addr, true); v.setUint32(20, size, true); }, 8);
  }

  write(addr, bytes) {
    return this.command(
      CMD_WRITE,
      v => { v.setUint32(16, addr, true); v.setUint32(20, bytes.byteLength, true); },
      8,
      bytes,
    );
  }

  /** pc=0,sp=0 means "boot whatever is now in flash". */
  reboot(delayMs = 500) {
    return this.command(CMD_REBOOT, v => {
      v.setUint32(16, 0, true);
      v.setUint32(20, 0, true);
      v.setUint32(24, delayMs, true);
    }, 12);
  }
}

/**
 * Flash a UF2 over USB.
 * @param {ArrayBuffer} uf2
 * @param {(pct:number, note:string)=>void} onProgress
 */
export async function flashOverUsb(uf2, onProgress = () => { }) {
  const image = parseUf2(uf2);
  onProgress(0, `${(image.bytes / 1024).toFixed(0)} KiB in ${image.runs.length} region(s)`);

  const boot = await Picoboot.request();
  try {
    onProgress(0.02, 'claiming device');
    await boot.exclusiveAccess(1);
    await boot.exitXip();

    let done = 0;
    for (const run of image.runs) {
      if (run.addr < FLASH_BASE) throw new Error(`region 0x${run.addr.toString(16)} is not in flash`);

      const eraseStart = run.addr & ~(SECTOR - 1);
      const eraseEnd = (run.addr + run.length + SECTOR - 1) & ~(SECTOR - 1);
      onProgress(done / image.bytes, `erasing 0x${eraseStart.toString(16)}`);
      await boot.erase(eraseStart, eraseEnd - eraseStart);

      for (let off = 0; off < run.length; off += CHUNK) {
        const slice = run.data.subarray(off, Math.min(off + CHUNK, run.length));
        await boot.write(run.addr + off, slice);
        done += slice.length;
        onProgress(done / image.bytes, `writing 0x${(run.addr + off).toString(16)}`);
      }
    }

    onProgress(1, 'rebooting into the new firmware');
    await boot.reboot(500);
  } finally {
    await boot.close();
  }
  return image;
}

/**
 * Fallback: write the UF2 onto the mounted RPI-RP2 volume. The user picks the
 * drive once; the bootloader reboots itself when the copy finishes.
 */
export async function flashViaDrive(uf2, filename, onProgress = () => { }) {
  if (!driveSupported) throw new Error('this browser cannot write to folders — use Chrome or Edge');
  onProgress(0, 'pick the RPI-RP2 drive');
  const dir = await window.showDirectoryPicker({ mode: 'readwrite', id: 'rpi-rp2' });

  // Sanity check: the bootloader volume always carries INFO_UF2.TXT.
  let looksRight = false;
  for await (const [name] of dir.entries()) {
    if (name.toUpperCase() === 'INFO_UF2.TXT') { looksRight = true; break; }
  }
  if (!looksRight) throw new Error('that folder is not an RP2 bootloader drive (no INFO_UF2.TXT)');

  onProgress(0.3, 'copying');
  const handle = await dir.getFileHandle(filename, { create: true });
  const w = await handle.createWritable();
  await w.write(uf2);
  await w.close();
  onProgress(1, 'copied — the board reboots itself');
}

/**
 * Kick a running board into BOOTSEL with the 1200-baud touch on its USB CDC
 * port. Only works if the firmware exposes stdio over USB.
 */
export async function rebootToBootsel() {
  if (!serialSupported) throw new Error('WebSerial is unavailable — use Chrome or Edge');
  const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: VENDOR_RP }] });
  await port.open({ baudRate: 1200 });
  await new Promise(r => setTimeout(r, 120));
  try { await port.close(); } catch { }
}
