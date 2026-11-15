// Exports the 35 Material Design shapes (the QML-flavoured JS port in tools/material-shapes, Apache-2.0) to
// assets/shapes.txt, so the C++ shell can draw them with plain cubic Béziers and no JS runtime.
//   node tools/export-shapes.js
// File format, one shape per line:   Name x0 y0  c0x c0y c1x c1y x1 y1  ...   (start point, then 6 numbers per cubic)
const fs = require("fs");
const path = require("path");
const root = path.join(__dirname, "material-shapes");
const cache = new Map();

// QML .js files use `.pragma library` and `.import "x.js" as Name`, and every top-level declaration is public.
// Wrap each file so imports become require-like calls and the top-level names become its exports.
function load(rel, from = root) {
  const file = path.resolve(from, rel);
  if (cache.has(file)) return cache.get(file);
  let src = fs.readFileSync(file, "utf8");
  const names = [...src.matchAll(/^(?:function|class)\s+(\w+)|^(?:var|let|const)\s+(\w+)/gm)].map(m => m[1] || m[2]);
  src = src.replace(/^\.pragma.*$/gm, "").replace(/^\.import\s+"([^"]+)"\s+as\s+(\w+)\s*$/gm, (_, p, n) => `var ${n} = __load(${JSON.stringify(p)});`);
  const exports = {};
  cache.set(file, exports);  // before running, so circular imports see the (filling) object
  const fn = new Function("__load", `${src}\n;return {${[...new Set(names)].map(n => `${n}: typeof ${n} !== "undefined" ? ${n} : undefined`).join(",")}};`);
  Object.assign(exports, fn(p => load(p, path.dirname(file))));
  return exports;
}

const shapes = load("material-shapes.js");
const out = [];
for (const [fn, make] of Object.entries(shapes)) {
  const m = fn.match(/^get(\w+)$/);
  if (!m || typeof make !== "function") continue;
  const cubics = make().cubics;
  if (!cubics || !cubics.length) continue;
  const nums = [cubics[0].anchor0X, cubics[0].anchor0Y];
  for (const c of cubics) nums.push(c.control0X, c.control0Y, c.control1X, c.control1Y, c.anchor1X, c.anchor1Y);
  out.push(`${m[1]} ${nums.map(n => +n.toFixed(5)).join(" ")}`);
}
fs.writeFileSync(path.join(__dirname, "..", "assets", "shapes.txt"), out.join("\n") + "\n");
console.log(`${out.length} shapes -> assets/shapes.txt`);
