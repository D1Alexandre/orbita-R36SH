"""Run the launcher against a PortMaster interface and a recording executable."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

LAUNCHER = Path(__file__).resolve().parents[1] / "packaging" / "Orbita.sh"


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="orbita-launcher-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.ports = self.root / "ports with spaces"
        self.game = self.ports / "orbita"
        self.bin = self.game / "bin" / "orbita"
        self.bin.parent.mkdir(parents=True)
        shutil.copyfile(LAUNCHER, self.ports / "Orbita.sh")
        self.pm = self.root / "PortMaster"
        self.pm.mkdir()
        (self.pm / "control.txt").write_text('''
# A legacy helper reads an optional variable without a default.
printf 'optional_version=%s\\n' "$DEVICE_INFO_VERSION"
CFW_NAME=fixture
get_controls() { sdl_controllerconfig=fixture-pad; }
pm_platform_helper() { printf 'platform_prepared=%s\\n' "$1"; }
pm_finish() { printf 'platform_finished=yes\\n'; result=0; }
''')
        (self.pm / "mod_fixture.txt").write_text("export ORBITA_FIXTURE_MOD=loaded\n")
        self.bin.write_text('''#!/bin/bash
printf 'fixture_binary_started=yes\\npad=%s\\nmod=%s\\n' "$SDL_GAMECONTROLLERCONFIG" "$ORBITA_FIXTURE_MOD"
printf 'fixture argument=%s\\n' "$@"
echo 'fixture stderr' >&2
exit 17
''')
        self.bin.chmod(0o755)
        self.env = dict(os.environ, HOME=str(self.root / "home"),
                        XDG_DATA_HOME=str(self.root / "data"),
                        ORBITA_PORTMASTER_DIR=str(self.pm))
        for name in ("DEVICE_INFO_VERSION", "SDL_GAMECONTROLLERCONFIG", "ORBITA_LOG_DIR"):
            self.env.pop(name, None)

    def run_launcher(self, *args):
        # Explicit nounset also tests launch from an inherited strict shell.
        return subprocess.run(["bash", "-u", str(self.ports / "Orbita.sh"), *args],
                              env=self.env, text=True, capture_output=True, timeout=10)

    def log(self):
        return (self.game / "logs" / "last-run.log").read_text()

    def test_optional_portmaster_variable_does_not_abort_and_failure_is_logged(self):
        result = self.run_launcher("argument with spaces")
        self.assertEqual(result.returncode, 17, result.stderr)
        log = self.log()
        for expected in ("fixture_binary_started=yes", "pad=fixture-pad", "mod=loaded",
                         "platform_prepared=", "platform_finished=yes", "fixture stderr",
                         "fixture argument=argument with spaces", "application_exit=17", "launcher_exit=17"):
            self.assertIn(expected, log)
        self.assertNotIn("unbound variable", log)

    def test_diagnose_does_not_run_display_setup_or_cleanup(self):
        self.assertEqual(self.run_launcher("--diagnose").returncode, 17)
        log = self.log()
        self.assertIn("fixture argument=--diagnose", log)
        self.assertNotIn("platform_prepared=", log)
        self.assertNotIn("platform_finished=", log)

    def test_missing_executable_has_an_early_log(self):
        self.bin.unlink()
        self.assertEqual(self.run_launcher().returncode, 1)
        self.assertIn("Executável Linux ARM ausente", self.log())
        self.assertIn("launcher_exit=1", self.log())

    def test_unwritable_log_path_uses_ram(self):
        obstacle = self.root / "not-a-directory"
        obstacle.write_text("file")
        self.env["ORBITA_LOG_DIR"] = str(obstacle / "logs")
        result = self.run_launcher("--diagnose")
        self.assertEqual(result.returncode, 17)
        output = result.stdout.strip()
        self.assertTrue(output.startswith("Log do Orbita: "))
        path = Path(output[len("Log do Orbita: "):])
        self.assertEqual(path.parent, Path("/dev/shm/orbita-%d-logs" % os.getuid()))
        self.assertIn("fixture_binary_started=yes", path.read_text())


if __name__ == "__main__":
    unittest.main()
