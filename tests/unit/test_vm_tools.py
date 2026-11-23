"""Fixture tooling must leave foreign VMs and busy disks alone."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import nova


class FixtureOwnership(unittest.TestCase):
    def test_matching_name_in_another_checkout(self):
        def manager(command, *args):
            if command == "list":
                output = f'"{nova.TEST_VM}" {{id}}\n'
            elif command == "showvminfo":
                output = 'CfgFile="another-checkout.vbox"\nVMState="poweroff"\n'
            else:
                self.fail(f"unexpected mutation: {command}")
            return subprocess.CompletedProcess([], 0, output, "")

        with tempfile.TemporaryDirectory() as tmp, \
                patch.object(nova, "VM_BASEFOLDER", tmp), \
                patch.object(nova, "VBOX", sys.executable), \
                patch.object(nova, "vbox", manager):
            self.assertFalse(nova.ensure_test_vm()[0])

    def test_busy_disk_survives_failed_close(self):
        with tempfile.TemporaryDirectory() as tmp:
            disk = Path(tmp, "disk.vdi")
            disk.write_bytes(b"existing disk")
            with patch.object(nova, "vbox", return_value=subprocess.CompletedProcess(
                    [], 1, "", "medium in use")):
                rc, _ = nova._convert_fixture("source.raw", str(disk))
            self.assertNotEqual(rc, 0)
            self.assertEqual(disk.read_bytes(), b"existing disk")


if __name__ == "__main__":
    unittest.main()
