#!/usr/bin/env python3
import fcntl
import os
import sys

try:
    with open("/dev/kvm", "rb+", buffering=0) as kvm:
        if fcntl.ioctl(kvm, 0xAE00, 0) != 12:
            raise RuntimeError("unexpected KVM API version")
        os.close(fcntl.ioctl(kvm, 0xAE01, 0))
except (OSError, RuntimeError) as error:
    sys.exit(f"KVM unavailable: {error}")
print("KVM API 12 created an empty VM successfully.")
