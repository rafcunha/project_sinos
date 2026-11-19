// The Things Network (TTS v3) uplink payload formatter for the project_sinos node.
// Contract: firmware/node_device/docs/payload/payload_spec.md
// Tests:    uplink_formatter.test.js (node --test), vectors in ../tests/fixtures/payload_vectors.json
//
// Paste this whole file in the TTN Console: Application > Payload formatters > Uplink >
// "Custom Javascript formatter". Written in ES5 (no let/const/arrow functions) so it runs
// on the TTN JavaScript engine.

var FPORT = 1;

var TYPE_NAMES = { 0: "heartbeat", 1: "measurement", 2: "status" };
var TYPE_LENGTHS = { 0: 3, 1: 9, 2: 10 };
var SUPPORTED_VERSION = 0;

var RESET_CAUSES = ["unknown", "power_on", "pin", "software", "iwdg", "wwdg", "low_power", "other"];

function u16(b, i) {
  return (b[i] << 8) | b[i + 1];
}

function i16(b, i) {
  var v = u16(b, i);
  return v >= 0x8000 ? v - 0x10000 : v;
}

function decodeHeartbeat(b) {
  return { counter: u16(b, 1) };
}

function decodeMeasurement(b) {
  var distance = u16(b, 1);
  var battery = u16(b, 3);
  var temp = i16(b, 5);
  var flags = b[7];
  var samples = b[8];
  return {
    distance_m: distance === 0xffff ? null : distance / 1000,
    battery_v: battery === 0xffff ? null : battery / 1000,
    temperature_c: temp === 0x7fff ? null : temp / 10,
    flags: {
      sensor_error: (flags & 0x01) !== 0,
      unstable: (flags & 0x02) !== 0,
      temp_uncompensated: (flags & 0x04) !== 0,
      battery_low: (flags & 0x08) !== 0,
      first_after_boot: (flags & 0x10) !== 0
    },
    samples: samples === 0xff ? null : samples
  };
}

function decodeStatus(b, warnings) {
  var cause = b[4];
  var uptime = u16(b, 5);
  if (cause >= RESET_CAUSES.length) {
    warnings.push("reset_cause " + cause + " out of range, reported as unknown");
    cause = 0;
  }
  return {
    firmware: b[1] + "." + b[2] + "." + b[3],
    reset_cause: RESET_CAUSES[cause],
    uptime_h: uptime === 0xffff ? null : uptime,
    join_failures: b[7],
    uplink_failures: b[8],
    module_resets: b[9]
  };
}

function decodeUplink(input) {
  var b = input.bytes || [];
  var warnings = [];

  if (input.fPort !== FPORT) {
    return { errors: ["unexpected_fport: expected " + FPORT + ", got " + input.fPort], warnings: warnings };
  }
  if (b.length === 0) {
    return { errors: ["empty: no payload"], warnings: warnings };
  }

  var type = b[0] & 0x1f;
  var version = b[0] >> 5;
  var name = TYPE_NAMES[type];
  if (name === undefined) {
    return { errors: ["unknown_type: type " + type + " is reserved"], warnings: warnings };
  }
  if (version !== SUPPORTED_VERSION) {
    return { errors: ["unknown_version: " + name + " version " + version + " is not supported"], warnings: warnings };
  }
  if (b.length !== TYPE_LENGTHS[type]) {
    return {
      errors: ["bad_length: " + name + " needs " + TYPE_LENGTHS[type] + " bytes, got " + b.length],
      warnings: warnings
    };
  }

  var fields;
  if (type === 0) {
    fields = decodeHeartbeat(b);
  } else if (type === 1) {
    fields = decodeMeasurement(b);
  } else {
    fields = decodeStatus(b, warnings);
  }

  var data = { type: name, version: version };
  for (var k in fields) {
    if (Object.prototype.hasOwnProperty.call(fields, k)) {
      data[k] = fields[k];
    }
  }
  return { data: data, warnings: warnings };
}

// Node.js test hook; ignored by TTN (no "module" object there).
if (typeof module !== "undefined" && module.exports) {
  module.exports = { decodeUplink: decodeUplink };
}
