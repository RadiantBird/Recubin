"""Bounded binary protocol shared by the Recubin rendezvous client and server.

All integers use network byte order.  Every datagram starts with::

    magic[4]="RCBN", version:u8, type:u8, payload_length:u16, txid:u32

Candidate blocks are ``count:u8`` followed by ``type:u8, host:u32, port:u16``.
The message payloads are:

* CookieRequest ``nonce[16]``; CookieChallenge ``cookie[16]``.
* Create ``cookie[16], candidates``; Created
  ``room[8], token[16], peer_id:u32, epoch:u32``.
* Join ``cookie[16], room[8], candidates``; Joined
  ``own_token[16], own_peer_id:u32, epoch:u32, host_peer_id:u32,
  host_candidates``.
* PeerJoined ``peer_id:u32, token[16], candidates``.  It is also the
  asynchronous peer candidate-update/snapshot message.
* CandidateUpdate ``cookie[16], token[16], candidates``; its response contains
  ``epoch:u32``.
* Refresh ``cookie[16], token[16]``; its response contains ``epoch:u32``.
* Promote ``cookie[16], token[16], expected_epoch:u32``; its response contains
  ``new_epoch:u32``.
* Leave ``cookie[16], token[16]``; its response is empty.
* Error contains one :class:`ErrorCode` byte.

``token`` is a 128-bit admission/session token.  Requests containing it must
come from the address that registered it.  The cookie is a stateless truncated
HMAC over the observed source IPv4 address and port, and remains valid for the
life of the server process.
"""

from __future__ import annotations

import enum
import struct
from dataclasses import dataclass

MAGIC = b"RCBN"
VERSION = 1
MAX_DATAGRAM_SIZE = 1200
MAX_CANDIDATES = 32
ROOM_CODE_SIZE = 8
TOKEN_SIZE = 16
COOKIE_SIZE = 16

HEADER = struct.Struct("!4sBBHI")
CANDIDATE = struct.Struct("!BIH")
U32 = struct.Struct("!I")


class MessageType(enum.IntEnum):
    COOKIE_REQUEST = 1
    COOKIE_CHALLENGE = 2
    CREATE = 3
    CREATED = 4
    JOIN = 5
    JOINED = 6
    PEER_JOINED = 7
    CANDIDATE_UPDATE = 8
    REFRESH = 9
    PROMOTE = 10
    LEAVE = 11
    ERROR = 12


class ErrorCode(enum.IntEnum):
    MALFORMED = 1
    COOKIE_REQUIRED = 2
    INVALID_COOKIE = 3
    ROOM_NOT_FOUND = 4
    ROOM_FULL = 5
    ADMISSION_REJECTED = 6
    CONFLICT = 7
    RATE_LIMITED = 8


class CandidateType(enum.IntEnum):
    LOCAL = 0
    SERVER_REFLEXIVE = 1
    PEER_REFLEXIVE = 2


@dataclass(frozen=True)
class Packet:
    message_type: MessageType
    transaction_id: int
    payload: bytes


@dataclass(frozen=True)
class Candidate:
    candidate_type: CandidateType
    host: int
    port: int


class ProtocolError(ValueError):
    """Raised for invalid or unbounded wire data."""


def encode_packet(message_type: MessageType, transaction_id: int, payload: bytes = b"") -> bytes:
    if not 0 <= transaction_id <= 0xFFFFFFFF:
        raise ProtocolError("transaction ID is outside uint32")
    if len(payload) > MAX_DATAGRAM_SIZE - HEADER.size:
        raise ProtocolError("payload is too large")
    try:
        wire_type = MessageType(message_type)
    except ValueError as error:
        raise ProtocolError("unknown message type") from error
    return HEADER.pack(MAGIC, VERSION, wire_type, len(payload), transaction_id) + payload


def decode_packet(data: bytes) -> Packet:
    if len(data) < HEADER.size or len(data) > MAX_DATAGRAM_SIZE:
        raise ProtocolError("invalid datagram size")
    magic, version, raw_type, payload_size, transaction_id = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ProtocolError("invalid magic")
    if version != VERSION:
        raise ProtocolError("unsupported version")
    try:
        message_type = MessageType(raw_type)
    except ValueError as error:
        raise ProtocolError("unknown message type") from error
    if payload_size != len(data) - HEADER.size:
        raise ProtocolError("payload length mismatch")
    return Packet(message_type, transaction_id, data[HEADER.size:])


def encode_candidates(candidates: list[Candidate] | tuple[Candidate, ...]) -> bytes:
    if len(candidates) > MAX_CANDIDATES:
        raise ProtocolError("too many candidates")
    output = bytearray((len(candidates),))
    for candidate in candidates:
        try:
            candidate_type = CandidateType(candidate.candidate_type)
        except ValueError as error:
            raise ProtocolError("unknown candidate type") from error
        if not 0 <= candidate.host <= 0xFFFFFFFF or not 1 <= candidate.port <= 0xFFFF:
            raise ProtocolError("invalid candidate address")
        output.extend(CANDIDATE.pack(candidate_type, candidate.host, candidate.port))
    return bytes(output)


def decode_candidates(data: bytes) -> list[Candidate]:
    if not data:
        raise ProtocolError("candidate count is missing")
    count = data[0]
    if count > MAX_CANDIDATES:
        raise ProtocolError("too many candidates")
    if len(data) != 1 + count * CANDIDATE.size:
        raise ProtocolError("candidate payload length mismatch")
    candidates: list[Candidate] = []
    offset = 1
    for _ in range(count):
        raw_type, host, port = CANDIDATE.unpack_from(data, offset)
        try:
            candidate_type = CandidateType(raw_type)
        except ValueError as error:
            raise ProtocolError("unknown candidate type") from error
        if port == 0:
            raise ProtocolError("candidate port is zero")
        candidates.append(Candidate(candidate_type, host, port))
        offset += CANDIDATE.size
    return candidates
