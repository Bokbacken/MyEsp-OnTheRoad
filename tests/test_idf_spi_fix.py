import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("spi_fix", ROOT / "tools/apply_idf_spi_fix.py")
fix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fix)


class SpiFixTests(unittest.TestCase):
    def test_transform_uses_stable_checked_pointer(self):
        original = "\n".join(before for before, _ in fix.REPLACEMENTS).encode()
        result = fix.transform(original)
        self.assertNotIn(b"resume_dev_in_isr(lock->acquiring_dev", result)
        self.assertIn(b"resume_dev_in_isr(acquiring_dev, do_yield)", result)
        self.assertIn(b"resume_dev_in_isr(desired_dev, do_yield)", result)
        self.assertIn(b"assert(desired_dev);", result)
        for before, _ in fix.REPLACEMENTS:
            with self.assertRaises(ValueError):
                fix.transform(original.replace(before.encode(), b"", 1))
        with self.assertRaises(ValueError):
            fix.transform(original + original)

    def test_verified_copy_keeps_sdk_unchanged(self):
        original = "\n".join(before for before, _ in fix.REPLACEMENTS).encode()
        expected = fix.transform(original)
        with tempfile.TemporaryDirectory() as tmp:
            source, output = Path(tmp) / "sdk.c", Path(tmp) / "fixed.c"
            source.write_bytes(original)
            with patch.object(fix, "ORIGINAL_SHA256", fix.digest(original)), \
                    patch.object(fix, "PATCHED_SHA256", fix.digest(expected)):
                fix.apply(source, output)
                fix.apply(source, output)
                self.assertEqual(source.read_bytes(), original)
                self.assertEqual(output.read_bytes(), expected)
                source.write_bytes(expected)
                fix.apply(source, output)
                self.assertEqual(output.read_bytes(), expected)
                with self.assertRaises(ValueError):
                    fix.apply(source, source)

    def test_unknown_sdk_and_invalid_output_are_rejected_before_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, output = Path(tmp) / "sdk.c", Path(tmp) / "fixed.c"
            source.write_bytes(b"unknown SDK")
            output.write_bytes(b"keep previous file")
            with self.assertRaises(ValueError):
                fix.apply(source, output)
            with patch.object(fix, "ORIGINAL_SHA256", fix.digest(source.read_bytes())), \
                    patch.object(fix, "transform", return_value=b"wrong patched source"):
                with self.assertRaises(ValueError):
                    fix.apply(source, output)
            self.assertEqual(output.read_bytes(), b"keep previous file")
