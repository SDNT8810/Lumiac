"""Check target selection and deployment boundaries without touching hardware."""

import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import build


class BuildCommandTests(unittest.TestCase):
    def run_main(self, args):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return build.main(args)

    def test_esp_aliases_select_correct_environment_and_port(self):
        for alias, environment, port in (
            ("esp12", "esp12s", "COM5"),
            ("eps12", "esp12s", "COM5"),
            ("esp12s", "esp12s", "COM5"),
            ("esp32", "esp32dev", "COM4"),
            ("eps32", "esp32dev", "COM4"),
        ):
            with self.subTest(alias=alias), patch("build.subprocess.run") as run, \
                    patch("build.resolve_project_dir", side_effect=AssertionError("ESP build must not resolve Marlin")), \
                    patch("build.remove_old_cur") as cleanup, patch("build.copy_firmware") as copy:
                self.assertEqual(self.run_main([alias, port]), 0)
                run.assert_called_once_with(
                    [sys.executable, "-m", "platformio", "run", "-d", str(build.ESP_PROJECT_DIR),
                     "-e", environment, "-t", "upload", "--upload-port", port], check=True,
                )
                cleanup.assert_not_called()
                copy.assert_not_called()

    def test_esp_without_port_or_with_build_only_does_not_upload(self):
        for args in (["esp12"], ["eps32", "COM4", "--build-only"]):
            with self.subTest(args=args), patch("build.subprocess.run") as run:
                self.assertEqual(self.run_main(args), 0)
                self.assertNotIn("upload", run.call_args.args[0])
                self.assertNotIn("--upload-port", run.call_args.args[0])

    def test_invalid_arguments_do_not_start_builds(self):
        for args in (["unknown", "COM5"], ["marlin", "COM5"], ["marlin-max", "COM5"]):
            with self.subTest(args=args), patch("build.subprocess.run") as run:
                with self.assertRaises(SystemExit) as raised:
                    self.run_main(args)
                self.assertEqual(raised.exception.code, 2)
                run.assert_not_called()

    def test_default_marlin_build_preserves_sd_copy_behavior(self):
        with tempfile.TemporaryDirectory(prefix="lumiac-build-test-") as folder:
            root = Path(folder)
            project = root / "Marlin"
            firmware = project / ".pio" / "build" / build.BUILD_ENV / "firmware.bin"
            firmware.parent.mkdir(parents=True)
            firmware.write_bytes(b"compiled-marlin")
            sd = root / "sd"
            sd.mkdir()
            marker = sd / "FIRMWARE.CUR"
            marker.write_bytes(b"old-marker")
            with patch("build.resolve_project_dir", return_value=project), \
                    patch("build.SD_ROOT", sd), patch("build.subprocess.run") as run:
                self.assertEqual(self.run_main([]), 0)
                self.assertIn(build.BUILD_ENV, run.call_args.args[0])
            self.assertFalse(marker.exists())
            self.assertEqual((sd / "firmware.bin").read_bytes(), b"compiled-marlin")

    def test_marlin_build_only_leaves_sd_untouched(self):
        with patch("build.subprocess.run"), patch("build.remove_old_cur") as cleanup, \
                patch("build.copy_firmware") as copy:
            self.assertEqual(self.run_main(["marlin", "--build-only"]), 0)
            cleanup.assert_not_called()
            copy.assert_not_called()

    def test_max_profile_uses_separate_environment_without_sd_writes(self):
        with patch("build.subprocess.run") as run, patch("build.remove_old_cur") as cleanup, \
                patch("build.copy_firmware") as copy:
            self.assertEqual(self.run_main(["marlin-max", "--build-only"]), 0)
            self.assertEqual(run.call_args.args[0][-2:], ["-e", "STM32F446ZE_btt_max_power"])
            cleanup.assert_not_called()
            copy.assert_not_called()

    def test_max_profile_copies_its_own_artifact(self):
        with tempfile.TemporaryDirectory(prefix="lumiac-build-test-") as folder:
            project = Path(folder)
            for environment, contents in ((build.BUILD_ENV, b"normal"), ("STM32F446ZE_btt_max_power", b"max-profile")):
                firmware = project / ".pio" / "build" / environment / "firmware.bin"
                firmware.parent.mkdir(parents=True)
                firmware.write_bytes(contents)
            with patch("build.resolve_project_dir", return_value=project), patch("build.subprocess.run"), \
                    patch("build.remove_old_cur"), patch("build.copy_firmware") as copy:
                self.assertEqual(self.run_main(["marlin-max"]), 0)
                self.assertEqual(copy.call_args.args[0].read_bytes(), b"max-profile")

    def test_failed_build_does_not_modify_sd(self):
        with patch("build.subprocess.run", side_effect=subprocess.CalledProcessError(7, ["platformio"])), \
                patch("build.remove_old_cur") as cleanup, patch("build.copy_firmware") as copy:
            self.assertEqual(self.run_main([]), 7)
            cleanup.assert_not_called()
            copy.assert_not_called()

    def test_missing_artifact_does_not_remove_sd_marker(self):
        with tempfile.TemporaryDirectory(prefix="lumiac-build-test-") as folder, \
                patch("build.resolve_project_dir", return_value=Path(folder)), \
                patch("build.subprocess.run"), patch("build.remove_old_cur") as cleanup:
            self.assertEqual(self.run_main([]), 1)
            cleanup.assert_not_called()

    def test_pt_entry_point_exposes_cli(self):
        result = subprocess.run(
            [sys.executable, str(build.ROOT / "build.pt"), "--help"],
            capture_output=True, text=True, check=True,
        )
        self.assertIn("eps12", result.stdout)
        self.assertIn("--build-only", result.stdout)


if __name__ == "__main__":
    unittest.main()
