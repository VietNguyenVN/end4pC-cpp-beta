"""Tests of scripts/audio_devices.py on a recorded system (tests/audio/fixtures/system.json): no pactl or Bluetooth needed."""
import importlib.util
import json
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "scripts", "audio_devices.py")
FIXTURE = os.path.join(HERE, "fixtures", "system.json")

spec = importlib.util.spec_from_file_location("audio_devices", SCRIPT)
ad = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ad)


def rows(backend=None):
    backend = backend or ad.Fixture(FIXTURE)
    return [dict(zip(["kind", "action", "target", "extra", "title", "sub", "icon", "tags", "default", "volume", "mute", "active"], r.split("\t")))
            for r in (ad.list_lines(backend)) if r.startswith(("sink\t", "source\t"))]


def by(kind, **match):
    return [r for r in rows() if r["kind"] == kind and all(r[k] == v for k, v in match.items())]


class ListTests(unittest.TestCase):
    def test_available_ports_are_rows_and_unavailable_port_is_hidden(self):
        ports = by("sink", target="alsa_output.pci-0000_00_1b.0.analog-stereo", action="set")
        self.assertEqual(sorted(r["title"] for r in ports), ["Headphones", "Speakers"])
        self.assertTrue(all(r["sub"] == "ALC221 Analog" for r in ports))

    def test_active_port_is_the_default_row_only(self):
        ports = by("sink", target="alsa_output.pci-0000_00_1b.0.analog-stereo")
        defaults = [r["extra"] for r in ports if r["default"] == "1"]
        self.assertEqual(defaults, ["analog-output-headphones"])

    def test_device_with_only_unplugged_ports_is_hidden(self):
        self.assertFalse(by("sink", target="alsa_output.pci-0000_01_00.1.hdmi-stereo"))

    def test_monitor_sources_are_skipped(self):
        self.assertFalse([r for r in rows() if "monitor" in r["target"]])

    def test_virtual_sink_is_kept_and_tagged(self):
        (row,) = by("sink", target="easyeffects_sink")
        self.assertIn("virtual", row["tags"].split(","))

    def test_unplugged_microphone_is_hidden_and_virtual_source_kept(self):
        self.assertFalse(by("source", target="alsa_input.pci-0000_00_1b.0.analog-stereo"))
        self.assertTrue(by("source", target="easyeffects_source"))

    def test_connected_bluetooth_sink_has_bt_tag_and_battery(self):
        (row,) = by("sink", target="bluez_output.AA_BB_CC_DD_EE_FF.1")
        self.assertIn("bt", row["tags"].split(","))
        self.assertIn("battery=80", row["tags"].split(","))
        self.assertEqual(row["icon"], "headphones-round")

    def test_profile_offered_when_available_or_unknown_only(self):
        offered = by("sink", action="profile")
        self.assertEqual(sorted(r["extra"] for r in offered), ["output:hdmi-surround"])
        self.assertEqual(offered[0]["icon"], "tv")

    def test_paired_disconnected_audio_device_offered_to_connect(self):
        (row,) = by("sink", action="connect")
        self.assertEqual(row["target"], "77:88:99:AA:BB:CC")
        self.assertEqual(row["title"], "Bose QC")

    def test_connected_and_non_audio_paired_devices_are_not_offered(self):
        macs = {r["target"] for r in rows() if r["action"] == "connect"}
        self.assertNotIn("AA:BB:CC:DD:EE:FF", macs)
        self.assertNotIn("11:22:33:44:55:66", macs)

    def test_every_device_row_has_twelve_fields_and_input_line_is_present(self):
        lines = ad.list_lines(ad.Fixture(FIXTURE))
        self.assertTrue(all(len(line.split("\t")) == 12 for line in lines if line.startswith(("sink\t", "source\t"))))
        self.assertEqual(lines[-1], "input\t100\t1")

    def test_device_rows_carry_their_own_volume_and_mute(self):
        for port in by("sink", target="alsa_output.pci-0000_00_1b.0.analog-stereo", action="set"):  # (every port of a device shows the device's volume)
            self.assertEqual((port["volume"], port["mute"]), ("34", "0"))
        (bt,) = by("sink", target="bluez_output.AA_BB_CC_DD_EE_FF.1")
        self.assertEqual((bt["volume"], bt["mute"]), ("50", "1"))

    def test_rows_that_are_not_devices_have_no_volume(self):
        (connect,) = by("sink", action="connect")
        self.assertEqual(connect["volume"], "-1")
        self.assertTrue(all(r["volume"] == "-1" for r in by("sink", action="profile")))

    def test_only_the_active_port_carries_the_volume_slider(self):
        ports = by("sink", target="alsa_output.pci-0000_00_1b.0.analog-stereo")
        self.assertEqual({r["extra"]: r["active"] for r in ports}, {"analog-output-headphones": "1", "analog-output-speaker": "0"})


class AppTests(unittest.TestCase):
    def apps(self):
        return [line.split("\t") for line in ad.list_lines(ad.Fixture(FIXTURE)) if line.startswith("app\t")]

    def test_playback_streams_are_listed_with_volume_mute_and_icon(self):
        by_index = {a[1]: a for a in self.apps()}
        self.assertEqual(by_index["11"][2:], ["Firefox", "firefox", "80", "0", "0", "YouTube - a song"])
        self.assertEqual(by_index["12"][2:7], ["Audacious", "audacious", "100", "1", "1"])

    def test_generic_media_names_are_dropped(self):
        by_index = {a[1]: a for a in self.apps()}
        self.assertEqual(by_index["12"][7], "")

    def test_equalizer_and_nameless_streams_are_not_apps(self):
        self.assertEqual(sorted(a[1] for a in self.apps()), ["11", "12"])


class ActionTests(unittest.TestCase):
    def test_set_with_port_sets_port_default_and_moves_streams(self):
        backend = ad.Fixture(FIXTURE)
        code = ad.run_action(backend, ["set", "sink", "alsa_output.pci-0000_00_1b.0.analog-stereo", "analog-output-speaker"])
        self.assertEqual(code, 0)
        self.assertIn(["pactl", "set-sink-port", "alsa_output.pci-0000_00_1b.0.analog-stereo", "analog-output-speaker"], backend.calls)
        self.assertIn(["pactl", "set-default-sink", "alsa_output.pci-0000_00_1b.0.analog-stereo"], backend.calls)
        self.assertIn(["pactl", "move-sink-input", "11", "alsa_output.pci-0000_00_1b.0.analog-stereo"], backend.calls)

    def test_source_move_skips_monitor_recordings(self):
        backend = ad.Fixture(FIXTURE)
        ad.run_action(backend, ["set", "source", "easyeffects_source"])
        self.assertFalse([c for c in backend.calls if c[1] == "move-source-output"])  # output 12 records a monitor

    def test_unknown_device_is_refused_without_calls(self):
        backend = ad.Fixture(FIXTURE)
        self.assertEqual(ad.run_action(backend, ["set", "sink", "nope"]), 1)
        self.assertEqual(backend.calls, [])

    def test_profile_action_uses_the_device_that_appears(self):
        class Appears(ad.Fixture):
            def lists(self, what):
                data = super().lists(what)
                if what == "sinks" and self.calls and self.calls[-1][:2] == ["pactl", "set-card-profile"]:
                    return data + [{"name": "alsa_output.gpu.hdmi-surround", "description": "HDMI 5.1", "ports": None, "properties": {}}]
                return data
        backend = Appears(FIXTURE)
        self.assertEqual(ad.run_action(backend, ["profile", "sink", "alsa_card.pci-0000_01_00.1", "output:hdmi-surround"]), 0)
        self.assertIn(["pactl", "set-default-sink", "alsa_output.gpu.hdmi-surround"], backend.calls)

    def test_connect_action_connects_then_uses_new_sink(self):
        class Connects(ad.Fixture):
            def lists(self, what):
                data = super().lists(what)
                if what == "sinks" and self.calls and self.calls[-1][:2] == ["bluetoothctl", "connect"]:
                    return data + [{"name": "bluez_output.77_88.1", "description": "Bose QC", "ports": None, "properties": {}}]
                return data
        backend = Connects(FIXTURE)
        self.assertEqual(ad.run_action(backend, ["connect", "sink", "77:88:99:AA:BB:CC"]), 0)
        self.assertIn(["bluetoothctl", "connect", "77:88:99:AA:BB:CC"], backend.calls)
        self.assertIn(["pactl", "set-default-sink", "bluez_output.77_88.1"], backend.calls)

    def test_profile_that_brings_no_device_returns_failure(self):
        backend = ad.Fixture(FIXTURE)
        self.assertEqual(ad.run_action(backend, ["profile", "sink", "alsa_card.pci-0000_01_00.1", "output:hdmi-surround"]), 1)


class AvailTests(unittest.TestCase):
    def test_availability_forms(self):
        self.assertEqual([ad.avail(v) for v in (2, 1, 0, "yes", "no", "not available", "available", "unknown", None)],
                         ["yes", "no", "unknown", "yes", "no", "no", "yes", "unknown", "unknown"])


if __name__ == "__main__":
    unittest.main()
