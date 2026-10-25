#!/usr/bin/env python3
"""Builds and decodes MIDI-CV Alpha SysEx configuration messages.

Without extra packages it prints the bytes as hex, ready to paste into any
SysEx tool (MIDI-OX "SysEx" window, a DAW, etc.). With `mido` and
`python-rtmidi` installed (pip install mido python-rtmidi) it can also send
the message to the board and print the replies.

examples:
  mcv_sysex.py get env               # read a parameter
  mcv_sysex.py set env 5000          # change it (applied right away)
  mcv_sysex.py save                  # store in flash
  mcv_sysex.py dump                  # all parameters
  mcv_sysex.py diag
  mcv_sysex.py decode "F0 7D 4D 00 41 22 03 7F 6C 3C 52 F7"
  mcv_sysex.py --send set lfo 2000   # send with mido (port "MIDI-CV Alpha")

Parameter names and ids are the same as the console's "p" command; see
docs/SYSEX.md for the table.
"""
import sys

MANUFACTURER, PRODUCT, DEVICE = 0x7D, 0x4D, 0x00
CMDS = {"get": 0x01, "set": 0x02, "save": 0x03, "dump": 0x04, "factory": 0x05,
        "calunlock": 0x06, "calset": 0x07, "diag": 0x08}
REPLIES = {0x41: "VALUE", 0x7E: "ACK", 0x7F: "NACK"}
ERRORS = {1: "bad CRC", 2: "unknown command", 3: "unknown parameter", 4: "value out of range",
          5: "locked (CAL_UNLOCK first)", 6: "bad length", 7: "flash write failed"}

# id: name, same table as firmware/src/params.c
PARAMS = {
    1: "channel", 2: "zero", 3: "bend", 4: "retrig", 5: "clockdiv", 6: "clockpulse", 7: "gateinv",
    10: "jack.src", 11: "jack.cc", 12: "jack.smooth", 13: "jack.range", 14: "jack.cal0", 15: "jack.gain",
    20: "filter.src", 21: "filter.cc", 22: "filter.smooth", 23: "filter.range", 24: "filter.cal0", 25: "filter.gain",
    30: "attack", 31: "decay", 32: "sustain", 33: "release", 34: "env", 35: "envvel", 36: "envretrig",
    40: "lfoshape", 41: "lforate", 42: "lfo", 43: "lfosync", 44: "lfowheel", 45: "keytrack",
    50: "glide", 51: "glidemode", 52: "glidelegato",
    0x00: "protocol", 0x70: "fw_version", 0x71: "chip_id", 0x72: "hse_mhz", 0x73: "midi_msgs",
    0x74: "din_errors", 0x75: "reset_flags",
}
NAMES = {v: k for k, v in PARAMS.items()}


def crc7(data):
    """CRC-7/MMC: poly x^7 + x^3 + 1, init 0, MSB first."""
    crc = 0
    for b in data:
        for i in range(7, -1, -1):
            bit = ((b >> i) & 1) ^ ((crc >> 6) & 1)
            crc = (crc << 1) & 0x7F
            if bit:
                crc ^= 0x09
    return crc


def encode_value(v):
    u = v & 0x1FFFFF
    return [(u >> 14) & 0x7F, (u >> 7) & 0x7F, u & 0x7F]


def decode_value(b):
    u = (b[0] << 14) | (b[1] << 7) | b[2]
    return u - 0x200000 if u & 0x100000 else u


def build(cmd, param=0, value=None, data=None):
    payload = data if data is not None else (encode_value(value) if value is not None else [])
    body = [PRODUCT, DEVICE, cmd & 0x7F, param & 0x7F, len(payload)] + payload
    return [0xF0, MANUFACTURER] + body + [crc7(body), 0xF7]


def decode(msg):
    if len(msg) < 9 or msg[0] != 0xF0 or msg[-1] != 0xF7 or msg[1] != MANUFACTURER or msg[2] != PRODUCT:
        return "not a MIDI-CV Alpha message"
    cmd, param, n = msg[4], msg[5], msg[6]
    data = msg[7:7 + n]
    ok = crc7(msg[2:7 + n]) == msg[7 + n]
    kind = REPLIES.get(cmd, "cmd 0x%02X" % cmd)
    text = kind
    if cmd == 0x41:
        text += " %s = %d" % (PARAMS.get(param, "id %d" % param), decode_value(data))
    elif cmd == 0x7E:
        text += " for cmd 0x%02X" % param
    elif cmd == 0x7F:
        text += " for cmd 0x%02X: %s" % (param, ERRORS.get(decode_value(data), "error"))
    return text + ("" if ok else "  (CRC MISMATCH)")


def hexs(msg):
    return " ".join("%02X" % b for b in msg)


def parse_args(args):
    if not args:
        sys.exit(__doc__)
    cmd = args[0].lower()
    if cmd == "decode":
        return None, [int(x, 16) for x in " ".join(args[1:]).split()]
    if cmd not in CMDS:
        sys.exit("unknown command %r, use: %s" % (cmd, ", ".join(CMDS)))
    if cmd in ("get", "set", "calset"):
        if len(args) < 2:
            sys.exit("%s needs a parameter name" % cmd)
        name = args[1]
        pid = NAMES.get(name, None)
        if pid is None:
            pid = int(name, 0)
        value = int(args[2], 0) if cmd != "get" else None
        if cmd != "get" and len(args) < 3:
            sys.exit("%s needs a value" % cmd)
        return build(CMDS[cmd], pid, value), None
    if cmd == "calunlock":
        return build(CMDS[cmd], 0, data=[ord("C"), ord("A"), ord("L")]), None
    return build(CMDS[cmd]), None


def send(msg):
    try:
        import mido
    except ImportError:
        sys.exit("--send needs: pip install mido python-rtmidi")
    out_name = next((n for n in mido.get_output_names() if "MIDI-CV" in n), None)
    in_name = next((n for n in mido.get_input_names() if "MIDI-CV" in n), None)
    if not out_name:
        sys.exit("MIDI-CV Alpha not found among MIDI outputs: %s" % mido.get_output_names())
    import time
    with mido.open_output(out_name) as out, mido.open_input(in_name) as inp:
        out.send(mido.Message("sysex", data=msg[1:-1]))
        deadline = time.time() + 1.0
        while time.time() < deadline:
            for m in inp.iter_pending():
                if m.type == "sysex":
                    reply = [0xF0] + list(m.data) + [0xF7]
                    print(decode(reply))
                    if reply[4] in (0x7E, 0x7F) or (reply[4] == 0x41 and msg[4] == 0x01):
                        return
            time.sleep(0.01)
        print("no reply")


def main():
    args = sys.argv[1:]
    do_send = "--send" in args
    args = [a for a in args if a != "--send"]
    msg, to_decode = parse_args(args)
    if to_decode is not None:
        print(decode(to_decode))
        return
    print(hexs(msg))
    if do_send:
        send(msg)


if __name__ == "__main__":
    main()
