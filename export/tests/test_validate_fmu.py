#!/usr/bin/env python3
"""
Unit tests for validate_fmu.py validator features, negative cases, and lxml schema include resolution.
"""

import argparse
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


class TestFMUValidator(unittest.TestCase):
    fmu4cpp_root = None
    build_dir = None

    @classmethod
    def setUpClass(cls):
        if cls.fmu4cpp_root is None:
            cls.fmu4cpp_root = Path(__file__).resolve().parent.parent.parent
        if cls.build_dir is None:
            cls.build_dir = cls.fmu4cpp_root / "build"
        cls.validator_script = cls.fmu4cpp_root / "scripts" / "validate_fmu.py"
        cls.schemas_dir = cls.fmu4cpp_root / "schemas"

    def run_validator(self, args):
        cmd = [sys.executable, str(self.validator_script)] + args
        return subprocess.run(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

    def test_lxml_schema_includes_resolution(self):
        """P0-1 regression test: verify lxml resolves relative xs:include without errors."""
        try:
            from lxml import etree
        except ImportError:
            self.skipTest("lxml not installed")

        xsd_path = self.schemas_dir / "fmi3" / "fmi3ModelDescription.xsd"
        self.assertTrue(xsd_path.exists(), f"XSD missing: {xsd_path}")

        # Parsing directly via etree.parse preserves base URL for relative xs:include
        schema_doc = etree.parse(str(xsd_path))
        schema = etree.XMLSchema(schema_doc)
        self.assertIsNotNone(schema)

    def test_valid_fmus_strict_auto_and_lxml(self):
        """Verify built FMUs pass strict validation with auto and lxml backends."""
        models_dir = self.build_dir / "models"
        if not models_dir.is_dir() or not list(models_dir.rglob("*.fmu")):
            self.skipTest("No FMUs found in build/models")

        # 1. auto backend
        res = self.run_validator(
            [
                "--dir",
                str(models_dir),
                "--strict",
                "--check-symbols",
                "--require-fmus",
                "--expect-models",
                "Identity",
                "BouncingBall",
                "--expect-generations",
                "fmi2",
                "fmi3",
            ]
        )
        self.assertEqual(
            res.returncode, 0, f"Validator auto failed:\n{res.stdout}\n{res.stderr}"
        )

        # 2. lxml backend if available
        try:
            import lxml

            res_lxml = self.run_validator(
                [
                    "--dir",
                    str(models_dir),
                    "--backend",
                    "lxml",
                    "--strict",
                    "--check-symbols",
                    "--require-fmus",
                    "--expect-models",
                    "Identity",
                    "BouncingBall",
                    "--expect-generations",
                    "fmi2",
                    "fmi3",
                ]
            )
            self.assertEqual(
                res_lxml.returncode,
                0,
                f"Validator lxml failed:\n{res_lxml.stdout}\n{res_lxml.stderr}",
            )
        except ImportError:
            pass

    def test_strict_missing_schema_dir(self):
        """P0-2: Verify missing schema directory causes strict validation to fail."""
        models_dir = self.build_dir / "models"
        if not models_dir.is_dir():
            self.skipTest("No build/models directory")

        with tempfile.TemporaryDirectory() as td:
            non_existent = Path(td) / "non_existent_schemas"
            res = self.run_validator(
                [
                    "--dir",
                    str(models_dir),
                    "--strict",
                    "--schema-dir",
                    str(non_existent),
                ]
            )
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("Could not locate schemas directory", res.stderr)

    def test_strict_missing_binaries(self):
        """P0-2: Verify missing binaries directory causes strict validation to fail."""
        sample_xml = (
            self.build_dir / "models" / "fmi3" / "Identity" / "modelDescription.xml"
        )
        if not sample_xml.exists():
            self.skipTest("Sample modelDescription.xml not found")

        with tempfile.TemporaryDirectory() as td:
            bad_fmu = Path(td) / "MissingBin.fmu"
            with zipfile.ZipFile(bad_fmu, "w") as z:
                z.write(sample_xml, arcname="modelDescription.xml")

            res = self.run_validator([str(bad_fmu), "--strict"])
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("No 'binaries/' found in FMU", res.stdout)

    def test_strict_wrong_binary_name(self):
        """P0-2: Verify non-matching binary filename causes strict validation to fail."""
        sample_xml = (
            self.build_dir / "models" / "fmi3" / "Identity" / "modelDescription.xml"
        )
        if not sample_xml.exists():
            self.skipTest("Sample modelDescription.xml not found")

        with tempfile.TemporaryDirectory() as td:
            bad_fmu = Path(td) / "WrongBinName.fmu"
            with zipfile.ZipFile(bad_fmu, "w") as z:
                z.write(sample_xml, arcname="modelDescription.xml")
                z.writestr("binaries/x86_64-linux/WrongName.so", "dummy")

            res = self.run_validator([str(bad_fmu), "--strict"])
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("missing expected binary 'Identity_fmi3'", res.stdout)

    def test_corrupt_binary_symbol_check(self):
        """P0-2: Verify dynamic loader catches corrupted binary."""
        sample_xml = (
            self.build_dir / "models" / "fmi3" / "Identity" / "modelDescription.xml"
        )
        if not sample_xml.exists():
            self.skipTest("Sample modelDescription.xml not found")

        with tempfile.TemporaryDirectory() as td:
            bad_fmu = Path(td) / "CorruptBin.fmu"
            with zipfile.ZipFile(bad_fmu, "w") as z:
                z.write(sample_xml, arcname="modelDescription.xml")
                z.writestr("binaries/x86_64-linux/Identity_fmi3.so", "invalid elf data")

            res = self.run_validator([str(bad_fmu), "--strict", "--check-symbols"])
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("Failed to dynamically load", res.stdout)

    def test_expect_models_failure(self):
        """P0-2: Verify missing expected model causes validator to fail."""
        models_dir = self.build_dir / "models"
        if not models_dir.is_dir():
            self.skipTest("No build/models directory")

        res = self.run_validator(
            [
                "--dir",
                str(models_dir),
                "--strict",
                "--expect-models",
                "Identity",
                "NonExistentModel",
            ]
        )
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("Missing expected model(s): NonExistentModel", res.stderr)

    def test_expect_generations_failure(self):
        """P0-2: Verify missing expected generation causes validator to fail."""
        models_dir = self.build_dir / "models"
        if not models_dir.is_dir():
            self.skipTest("No build/models directory")

        res = self.run_validator(
            [
                "--dir",
                str(models_dir),
                "--strict",
                "--expect-generations",
                "fmi2",
                "fmi3",
                "fmi4",
            ]
        )
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("Missing expected generation(s): fmi4", res.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fmu4cpp-root", type=Path, default=None)
    parser.add_argument("--build-dir", type=Path, default=None)
    args, unknown = parser.parse_known_args()

    TestFMUValidator.fmu4cpp_root = args.fmu4cpp_root
    TestFMUValidator.build_dir = args.build_dir

    unittest.main(argv=[sys.argv[0]] + unknown)
