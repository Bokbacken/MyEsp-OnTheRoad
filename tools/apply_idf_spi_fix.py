"""Backport Espressif a16c003 to the exact ESP-IDF v6.1 SPI bus-lock source."""
import argparse
import hashlib
from pathlib import Path

ORIGINAL_SHA256 = "ee582cacb45862c7ab2a4be23a7b32db18a80e4c06cb834dbed64d3a313986c5"
PATCHED_SHA256 = "1833df943859862715c7ca047d40de432b75767cb2590f1f91d836bb4ead114e"
REPLACEMENTS = (
    (
        """    if (lock->acquiring_dev) {
        if (status & DEV_BG_MASK(lock->acquiring_dev)) {""",
        """    spi_bus_lock_dev_t *acquiring_dev = (spi_bus_lock_dev_t *)lock->acquiring_dev;
    if (acquiring_dev) {
        if (status & DEV_BG_MASK(acquiring_dev)) {""",
    ),
    (
        """            resume_dev_in_isr(lock->acquiring_dev, do_yield);
            ret = true;
        }
    } else {""",
        """            resume_dev_in_isr(acquiring_dev, do_yield);
            ret = true;
        }
    } else {""",
    ),
    (
        """            BUS_LOCK_DEBUG_EXECUTE_CHECK(bg_yield);
            BUS_LOCK_DEBUG_EXECUTE_CHECK(desired_dev);
            BUS_LOCK_DEBUG_EXECUTE_CHECK(lock->acquiring_dev == desired_dev);
            resume_dev_in_isr(lock->acquiring_dev, do_yield);""",
        """            assert(desired_dev);
            BUS_LOCK_DEBUG_EXECUTE_CHECK(bg_yield);
            BUS_LOCK_DEBUG_EXECUTE_CHECK(lock->acquiring_dev == desired_dev);
            resume_dev_in_isr(desired_dev, do_yield);""",
    ),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def transform(data):
    text = data.decode("utf-8")
    for before, after in REPLACEMENTS:
        if text.count(before) != 1:
            raise ValueError("SPI fix preimage does not match exactly once")
        text = text.replace(before, after, 1)
    return text.encode("utf-8")


def apply(source, output):
    original = source.read_bytes()
    if digest(original) == PATCHED_SHA256:
        patched = original
    elif digest(original) == ORIGINAL_SHA256:
        patched = transform(original)
    else:
        raise ValueError("Unrecognized SPI bus-lock source; review the IDF version before building")
    if digest(patched) != PATCHED_SHA256:
        raise ValueError("SPI fix output fingerprint mismatch")
    if source.resolve() == output.resolve():
        raise ValueError("SPI fix must not overwrite the installed SDK source")
    output.write_bytes(patched)
    print("Prepared Espressif a16c003 SPI bus-lock TOCTOU fix (verified SHA256)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    apply(args.source, args.output)


if __name__ == "__main__":
    main()
