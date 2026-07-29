"""Minimal RFC 8489 IPv4 STUN server for local Recubin integration tests."""

from __future__ import annotations

import argparse
import socket
import struct

try:
    from .protocol import MAX_DATAGRAM_SIZE
except ImportError:  # Allows ``python tools/rendezvous/fake_stun.py``.
    from protocol import MAX_DATAGRAM_SIZE  # type: ignore

BINDING_REQUEST = 0x0001
BINDING_SUCCESS_RESPONSE = 0x0101
XOR_MAPPED_ADDRESS = 0x0020
MAGIC_COOKIE = 0x2112A442
REQUEST_SIZE = 20

STUN_HEADER = struct.Struct("!HHI12s")
XOR_MAPPED_ATTRIBUTE = struct.Struct("!HHBBH4s")


def build_binding_response(data: bytes, source: tuple[str, int]) -> bytes | None:
    """Return a Binding Success Response, or ``None`` for malformed input."""
    if len(data) != REQUEST_SIZE or len(data) > MAX_DATAGRAM_SIZE:
        return None
    message_type, message_length, magic_cookie, transaction_id = STUN_HEADER.unpack(data)
    if (
        message_type != BINDING_REQUEST
        or message_length != 0
        or magic_cookie != MAGIC_COOKIE
    ):
        return None
    if not 1 <= source[1] <= 0xFFFF:
        return None
    try:
        source_address = socket.inet_aton(source[0])
    except OSError:
        return None

    cookie_bytes = struct.pack("!I", MAGIC_COOKIE)
    xored_address = bytes(left ^ right for left, right in zip(source_address, cookie_bytes))
    attribute = XOR_MAPPED_ATTRIBUTE.pack(
        XOR_MAPPED_ADDRESS,
        8,
        0,
        1,
        source[1] ^ (MAGIC_COOKIE >> 16),
        xored_address,
    )
    return STUN_HEADER.pack(
        BINDING_SUCCESS_RESPONSE,
        len(attribute),
        MAGIC_COOKIE,
        transaction_id,
    ) + attribute


def serve_forever(bind_host: str, port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
        udp_socket.bind((bind_host, port))
        print(f"Recubin fake STUN listening on {bind_host}:{udp_socket.getsockname()[1]}")
        while True:
            data, source = udp_socket.recvfrom(MAX_DATAGRAM_SIZE + 1)
            response = build_binding_response(data, source)
            if response is not None:
                udp_socket.sendto(response, source)


def main() -> None:
    parser = argparse.ArgumentParser(description="Recubin local-test IPv4 STUN server")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=3478)
    arguments = parser.parse_args()
    try:
        serve_forever(arguments.bind, arguments.port)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

