// Tests of the TTN uplink formatter against the shared vectors (payload_spec.md 5).
// Run: node --test firmware/node_device/ttn/uplink_formatter.test.js   (Node >= 18, no dependencies)
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const { decodeUplink } = require("./uplink_formatter.js");

const vectors = JSON.parse(
  fs.readFileSync(path.join(__dirname, "..", "tests", "fixtures", "payload_vectors.json"), "utf8")
);

function bytes(hex) {
  return Array.from(Buffer.from(hex, "hex"));
}

test("there are vectors to test", () => {
  assert.ok(vectors.valid.length >= 10);
  assert.ok(vectors.invalid.length >= 5);
});

for (const v of vectors.valid) {
  test(`valid: ${v.name}`, () => {
    const r = decodeUplink({ bytes: bytes(v.hex), fPort: 1 });
    assert.equal(r.errors, undefined, `unexpected errors: ${r.errors}`);
    assert.deepEqual(r.data, v.ttn);
    assert.equal(r.warnings.length, v.ttn_warnings || 0, `warnings: ${r.warnings}`);
  });
}

for (const v of vectors.invalid) {
  test(`invalid: ${v.name}`, () => {
    const r = decodeUplink({ bytes: bytes(v.hex), fPort: 1 });
    assert.equal(r.data, undefined);
    assert.equal(r.errors.length, 1);
    assert.ok(r.errors[0].startsWith(v.error + ":"), `got "${r.errors[0]}", want ${v.error}`);
  });
}

test("wrong fPort is an error", () => {
  const r = decodeUplink({ bytes: bytes("000001"), fPort: 2 });
  assert.equal(r.data, undefined);
  assert.ok(r.errors[0].startsWith("unexpected_fport:"));
});

test("missing bytes is treated as empty", () => {
  const r = decodeUplink({ fPort: 1 });
  assert.ok(r.errors[0].startsWith("empty:"));
});

test("formatter source is ES5 (TTN engine)", () => {
  const src = fs.readFileSync(path.join(__dirname, "uplink_formatter.js"), "utf8");
  assert.doesNotMatch(src, /\b(let|const)\s/, "use var");
  assert.doesNotMatch(src, /=>/, "no arrow functions");
  assert.doesNotMatch(src, /`/, "no template literals");
});
