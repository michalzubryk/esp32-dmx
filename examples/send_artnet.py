#!/usr/bin/env python3
"""Send one buffered ArtDMX universe. Default payload is all zeros."""
import argparse
import socket
import struct
import time


def packet(values, universe, sequence):
    return (b'Art-Net\0' + struct.pack('<H', 0x5000) + struct.pack('>H', 14)
            + bytes([sequence, 0]) + struct.pack('<H', universe)
            + struct.pack('>H', len(values)) + values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--universe', type=int, default=0)
    parser.add_argument('--address', type=int, default=1)
    parser.add_argument('--values', default='0')
    parser.add_argument('--seconds', type=float, default=2)
    parser.add_argument('--blackout', action='store_true')
    args = parser.parse_args()
    try:
        channels = [int(v.strip()) for v in args.values.split(',')]
    except ValueError:
        parser.error('values must be comma-separated integers')
    if not 0 <= args.universe <= 32767 or not 1 <= args.address <= 512:
        parser.error('invalid universe or address')
    if not channels or any(not 0 <= v <= 255 for v in channels) or args.address + len(channels) - 1 > 512:
        parser.error('channel range must fit DMX 1-512 and values 0-255')
    if not 0 < args.seconds <= 3600:
        parser.error('seconds must be in (0, 3600]')
    data = bytearray(512)
    data[args.address - 1:args.address - 1 + len(channels)] = bytes(channels)
    sequence = 0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        def send(payload):
            nonlocal sequence
            sequence = sequence % 255 + 1
            sock.sendto(packet(payload, args.universe, sequence), (args.host, 6454))
        try:
            end = time.monotonic() + args.seconds
            while time.monotonic() < end:
                send(data)
                time.sleep(0.04)
        finally:
            if args.blackout:
                for _ in range(25):
                    send(bytes(512))
                    time.sleep(0.04)


if __name__ == '__main__':
    main()
