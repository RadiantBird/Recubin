"""Standard-library-only IPv4 UDP rendezvous service for Recubin."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import os
import secrets
import socket
import struct
import time
from dataclasses import dataclass, field

try:
    from . import protocol
except ImportError:  # Allows ``python tools/rendezvous/server.py``.
    import protocol  # type: ignore

DEFAULT_PORT = 3479
DEFAULT_TTL_SECONDS = 30.0
DEFAULT_FAILOVER_GRACE_SECONDS = 10.0
DEFAULT_RATE = 10.0
DEFAULT_BURST = 20.0
ROOM_CAPACITY = 32
CROCKFORD_BASE32 = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

Address = tuple[str, int]
Outbound = tuple[bytes, Address]


@dataclass
class Participant:
    peer_id: int
    token: bytes
    address: Address
    candidates: list[protocol.Candidate]
    last_seen: float


@dataclass
class Room:
    code: bytes
    epoch: int = 1
    next_peer_id: int = 2
    host_token: bytes | None = None
    participants: dict[bytes, Participant] = field(default_factory=dict)


@dataclass
class Bucket:
    tokens: float
    updated_at: float


class RendezvousServer:
    def __init__(
        self,
        secret: bytes,
        *,
        ttl_seconds: float = DEFAULT_TTL_SECONDS,
        failover_grace_seconds: float = DEFAULT_FAILOVER_GRACE_SECONDS,
        rate: float = DEFAULT_RATE,
        burst: float = DEFAULT_BURST,
    ) -> None:
        if len(secret) < 16:
            raise ValueError("secret must contain at least 16 bytes")
        if (
            ttl_seconds <= 0
            or failover_grace_seconds <= 0
            or failover_grace_seconds >= ttl_seconds
            or rate <= 0
            or burst < 1
        ):
            raise ValueError("TTL, rate, and burst must be positive")
        self.secret = secret
        self.ttl_seconds = ttl_seconds
        self.failover_grace_seconds = failover_grace_seconds
        self.rate = rate
        self.burst = burst
        self.rooms: dict[bytes, Room] = {}
        self.token_rooms: dict[bytes, bytes] = {}
        self.buckets: dict[Address, Bucket] = {}

    def _cookie(self, address: Address) -> bytes:
        packed_address = socket.inet_aton(address[0]) + struct.pack("!H", address[1])
        return hmac.new(self.secret, packed_address, hashlib.sha256).digest()[: protocol.COOKIE_SIZE]

    def _valid_cookie(self, cookie: bytes, address: Address) -> bool:
        return hmac.compare_digest(cookie, self._cookie(address))

    def _allow(self, address: Address, now: float) -> bool:
        bucket = self.buckets.get(address)
        if bucket is None:
            self.buckets[address] = Bucket(self.burst - 1.0, now)
            return True
        elapsed = max(0.0, now - bucket.updated_at)
        bucket.tokens = min(self.burst, bucket.tokens + elapsed * self.rate)
        bucket.updated_at = now
        if bucket.tokens < 1.0:
            return False
        bucket.tokens -= 1.0
        return True

    def _cleanup(self, now: float) -> None:
        for room_code, room in list(self.rooms.items()):
            for token, participant in list(room.participants.items()):
                if now - participant.last_seen > self.ttl_seconds:
                    del room.participants[token]
                    self.token_rooms.pop(token, None)
                    if room.host_token == token:
                        room.host_token = None
            if not room.participants:
                del self.rooms[room_code]
        stale_bucket_age = max(self.ttl_seconds, 60.0)
        for address, bucket in list(self.buckets.items()):
            if now - bucket.updated_at > stale_bucket_age:
                del self.buckets[address]

    @staticmethod
    def _error(transaction_id: int, code: protocol.ErrorCode, address: Address) -> Outbound:
        return protocol.encode_packet(protocol.MessageType.ERROR, transaction_id, bytes((code,))), address

    @staticmethod
    def _observed_candidate(address: Address) -> protocol.Candidate:
        host = int.from_bytes(socket.inet_aton(address[0]), "big")
        return protocol.Candidate(protocol.CandidateType.PEER_REFLEXIVE, host, address[1])

    def _with_observed(
        self, candidates: list[protocol.Candidate], address: Address
    ) -> list[protocol.Candidate]:
        observed = self._observed_candidate(address)
        result = list(candidates)
        if observed not in result:
            if len(result) == protocol.MAX_CANDIDATES:
                result[-1] = observed
            else:
                result.append(observed)
        return result

    def _new_room_code(self) -> bytes:
        while True:
            code = "".join(secrets.choice(CROCKFORD_BASE32) for _ in range(protocol.ROOM_CODE_SIZE))
            encoded = code.encode("ascii")
            if encoded not in self.rooms:
                return encoded

    @staticmethod
    def _peer_payload(participant: Participant) -> bytes:
        return (
            struct.pack("!I", participant.peer_id)
            + participant.token
            + protocol.encode_candidates(participant.candidates)
        )

    @staticmethod
    def _joined_payload(participant: Participant, room: Room, host: Participant) -> bytes:
        return (
            participant.token
            + struct.pack("!III", participant.peer_id, room.epoch, host.peer_id)
            + protocol.encode_candidates(host.candidates)
        )

    def _participant_for_token(self, token: bytes) -> tuple[Room, Participant] | None:
        room_code = self.token_rooms.get(token)
        if room_code is None:
            return None
        room = self.rooms.get(room_code)
        if room is None:
            return None
        participant = room.participants.get(token)
        if participant is None:
            return None
        return room, participant

    def process_datagram(
        self, data: bytes, address: Address, *, now: float | None = None
    ) -> list[Outbound]:
        current_time = time.monotonic() if now is None else now
        if len(data) > protocol.MAX_DATAGRAM_SIZE:
            return []
        try:
            packet = protocol.decode_packet(data)
        except protocol.ProtocolError:
            return []
        if not self._allow(address, current_time):
            return [self._error(packet.transaction_id, protocol.ErrorCode.RATE_LIMITED, address)]
        self._cleanup(current_time)

        if packet.message_type == protocol.MessageType.COOKIE_REQUEST:
            if len(packet.payload) != protocol.COOKIE_SIZE:
                return [self._error(packet.transaction_id, protocol.ErrorCode.MALFORMED, address)]
            cookie = self._cookie(address)
            response = protocol.encode_packet(
                protocol.MessageType.COOKIE_CHALLENGE, packet.transaction_id, cookie
            )
            return [(response, address)]

        if packet.message_type not in {
            protocol.MessageType.CREATE,
            protocol.MessageType.JOIN,
            protocol.MessageType.CANDIDATE_UPDATE,
            protocol.MessageType.REFRESH,
            protocol.MessageType.PROMOTE,
            protocol.MessageType.LEAVE,
        }:
            return [self._error(packet.transaction_id, protocol.ErrorCode.MALFORMED, address)]
        if len(packet.payload) < protocol.COOKIE_SIZE:
            return [self._error(packet.transaction_id, protocol.ErrorCode.COOKIE_REQUIRED, address)]
        cookie, payload = packet.payload[: protocol.COOKIE_SIZE], packet.payload[protocol.COOKIE_SIZE :]
        if not self._valid_cookie(cookie, address):
            return [self._error(packet.transaction_id, protocol.ErrorCode.INVALID_COOKIE, address)]

        handlers = {
            protocol.MessageType.CREATE: self._create,
            protocol.MessageType.JOIN: self._join,
            protocol.MessageType.CANDIDATE_UPDATE: self._candidate_update,
            protocol.MessageType.REFRESH: self._refresh,
            protocol.MessageType.PROMOTE: self._promote,
            protocol.MessageType.LEAVE: self._leave,
        }
        try:
            return handlers[packet.message_type](packet.transaction_id, payload, address, current_time)
        except (protocol.ProtocolError, UnicodeError):
            return [self._error(packet.transaction_id, protocol.ErrorCode.MALFORMED, address)]

    def _create(self, transaction_id: int, payload: bytes, address: Address, now: float) -> list[Outbound]:
        candidates = self._with_observed(protocol.decode_candidates(payload), address)
        room_code = self._new_room_code()
        token = secrets.token_bytes(protocol.TOKEN_SIZE)
        participant = Participant(1, token, address, candidates, now)
        room = Room(room_code, host_token=token, participants={token: participant})
        self.rooms[room_code] = room
        self.token_rooms[token] = room_code
        response_payload = room_code + token + struct.pack("!II", participant.peer_id, room.epoch)
        return [
            (
                protocol.encode_packet(protocol.MessageType.CREATED, transaction_id, response_payload),
                address,
            )
        ]

    def _join(self, transaction_id: int, payload: bytes, address: Address, now: float) -> list[Outbound]:
        if len(payload) < protocol.ROOM_CODE_SIZE + 1:
            raise protocol.ProtocolError("join payload is truncated")
        room_code = payload[: protocol.ROOM_CODE_SIZE].upper()
        candidates = self._with_observed(
            protocol.decode_candidates(payload[protocol.ROOM_CODE_SIZE :]), address
        )
        room = self.rooms.get(room_code)
        if room is None or room.host_token is None:
            return [self._error(transaction_id, protocol.ErrorCode.ROOM_NOT_FOUND, address)]
        if len(room.participants) >= ROOM_CAPACITY:
            return [self._error(transaction_id, protocol.ErrorCode.ROOM_FULL, address)]
        host = room.participants.get(room.host_token)
        if host is None:
            return [self._error(transaction_id, protocol.ErrorCode.ROOM_NOT_FOUND, address)]

        existing_participants = list(room.participants.values())
        token = secrets.token_bytes(protocol.TOKEN_SIZE)
        participant = Participant(room.next_peer_id, token, address, candidates, now)
        room.next_peer_id += 1
        room.participants[token] = participant
        self.token_rooms[token] = room.code
        joined = protocol.encode_packet(
            protocol.MessageType.JOINED,
            transaction_id,
            self._joined_payload(participant, room, host),
        )
        peer_joined = protocol.encode_packet(
            protocol.MessageType.PEER_JOINED,
            transaction_id,
            self._peer_payload(participant),
        )
        responses = [(joined, address)]
        for existing in existing_participants:
            responses.append((peer_joined, existing.address))
            existing_snapshot = protocol.encode_packet(
                protocol.MessageType.PEER_JOINED,
                transaction_id,
                self._peer_payload(existing),
            )
            responses.append((existing_snapshot, address))
        return responses

    def _candidate_update(
        self, transaction_id: int, payload: bytes, address: Address, now: float
    ) -> list[Outbound]:
        if len(payload) < protocol.TOKEN_SIZE + 1:
            raise protocol.ProtocolError("candidate update is truncated")
        token = payload[: protocol.TOKEN_SIZE]
        found = self._participant_for_token(token)
        if found is None:
            return [self._error(transaction_id, protocol.ErrorCode.ADMISSION_REJECTED, address)]
        room, participant = found
        if participant.address != address:
            return [self._error(transaction_id, protocol.ErrorCode.ADMISSION_REJECTED, address)]
        participant.candidates = self._with_observed(
            protocol.decode_candidates(payload[protocol.TOKEN_SIZE :]), address
        )
        participant.last_seen = now
        responses: list[Outbound] = [
            (
                protocol.encode_packet(
                    protocol.MessageType.CANDIDATE_UPDATE,
                    transaction_id,
                    struct.pack("!I", room.epoch),
                ),
                address,
            )
        ]
        if room.host_token == token:
            for other in room.participants.values():
                if other.token != token:
                    responses.append(
                        (
                            protocol.encode_packet(
                                protocol.MessageType.JOINED,
                                transaction_id,
                                self._joined_payload(other, room, participant),
                            ),
                            other.address,
                        )
                    )
        else:
            peer_update = protocol.encode_packet(
                protocol.MessageType.PEER_JOINED,
                transaction_id,
                self._peer_payload(participant),
            )
            responses.extend(
                (peer_update, other.address)
                for other in room.participants.values()
                if other.token != token
            )
        return responses

    def _refresh(self, transaction_id: int, payload: bytes, address: Address, now: float) -> list[Outbound]:
        if len(payload) != protocol.TOKEN_SIZE:
            raise protocol.ProtocolError("refresh payload length mismatch")
        found = self._participant_for_token(payload)
        if found is None or found[1].address != address:
            return [self._error(transaction_id, protocol.ErrorCode.ADMISSION_REJECTED, address)]
        room, participant = found
        participant.last_seen = now
        response = protocol.encode_packet(
            protocol.MessageType.REFRESH, transaction_id, struct.pack("!I", room.epoch)
        )
        return [(response, address)]

    def _promote(self, transaction_id: int, payload: bytes, address: Address, now: float) -> list[Outbound]:
        if len(payload) != protocol.TOKEN_SIZE + 4:
            raise protocol.ProtocolError("promote payload length mismatch")
        token = payload[: protocol.TOKEN_SIZE]
        expected_epoch = struct.unpack("!I", payload[protocol.TOKEN_SIZE :])[0]
        found = self._participant_for_token(token)
        if found is None or found[1].address != address:
            return [self._error(transaction_id, protocol.ErrorCode.ADMISSION_REJECTED, address)]
        room, participant = found
        active_host = room.participants.get(room.host_token) if room.host_token else None
        if expected_epoch != room.epoch:
            return [self._error(transaction_id, protocol.ErrorCode.CONFLICT, address)]
        if active_host is not None:
            if now - active_host.last_seen <= self.failover_grace_seconds:
                return [self._error(transaction_id, protocol.ErrorCode.CONFLICT, address)]
            # ENet detects a dead host before the room TTL necessarily expires.  Once
            # several refresh periods have elapsed, evict that stale host atomically;
            # the first valid Promote wins by advancing the epoch.
            room.participants.pop(active_host.token, None)
            self.token_rooms.pop(active_host.token, None)

        room.host_token = token
        room.epoch += 1
        participant.last_seen = now
        responses: list[Outbound] = [
            (
                protocol.encode_packet(
                    protocol.MessageType.PROMOTE, transaction_id, struct.pack("!I", room.epoch)
                ),
                address,
            )
        ]
        for other in room.participants.values():
            if other.token == token:
                continue
            responses.append(
                (
                    protocol.encode_packet(
                        protocol.MessageType.JOINED,
                        transaction_id,
                        self._joined_payload(other, room, participant),
                    ),
                    other.address,
                )
            )
            responses.append(
                (
                    protocol.encode_packet(
                        protocol.MessageType.PEER_JOINED,
                        transaction_id,
                        self._peer_payload(other),
                    ),
                    participant.address,
                )
            )
        return responses

    def _leave(self, transaction_id: int, payload: bytes, address: Address, now: float) -> list[Outbound]:
        del now
        if len(payload) != protocol.TOKEN_SIZE:
            raise protocol.ProtocolError("leave payload length mismatch")
        found = self._participant_for_token(payload)
        if found is None or found[1].address != address:
            return [self._error(transaction_id, protocol.ErrorCode.ADMISSION_REJECTED, address)]
        room, participant = found
        del room.participants[participant.token]
        self.token_rooms.pop(participant.token, None)
        if room.host_token == participant.token:
            room.host_token = None
        if not room.participants:
            self.rooms.pop(room.code, None)
        response = protocol.encode_packet(protocol.MessageType.LEAVE, transaction_id)
        return [(response, address)]

    def serve_forever(self, bind_host: str, port: int) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
            udp_socket.bind((bind_host, port))
            print(f"Recubin rendezvous listening on {bind_host}:{udp_socket.getsockname()[1]}")
            while True:
                data, address = udp_socket.recvfrom(protocol.MAX_DATAGRAM_SIZE + 1)
                for response, destination in self.process_datagram(data, address):
                    udp_socket.sendto(response, destination)


def _secret_from_environment_or_argument(value: str | None) -> bytes:
    if value is None:
        return secrets.token_bytes(32)
    if value.startswith("hex:"):
        try:
            secret = bytes.fromhex(value[4:])
        except ValueError as error:
            raise argparse.ArgumentTypeError("invalid hexadecimal secret") from error
    else:
        secret = value.encode("utf-8")
    if len(secret) < 16:
        raise argparse.ArgumentTypeError("secret must contain at least 16 bytes")
    return secret


def main() -> None:
    parser = argparse.ArgumentParser(description="Recubin IPv4 UDP rendezvous service")
    parser.add_argument("--bind", default=os.environ.get("RECUBIN_RENDEZVOUS_BIND", "0.0.0.0"))
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("RECUBIN_RENDEZVOUS_PORT", DEFAULT_PORT)),
    )
    parser.add_argument(
        "--ttl",
        type=float,
        default=float(os.environ.get("RECUBIN_RENDEZVOUS_TTL", DEFAULT_TTL_SECONDS)),
    )
    parser.add_argument(
        "--secret",
        default=os.environ.get("RECUBIN_RENDEZVOUS_SECRET"),
        help="HMAC secret (plain text or hex:...), generated at startup if omitted",
    )
    arguments = parser.parse_args()
    server = RendezvousServer(
        _secret_from_environment_or_argument(arguments.secret),
        ttl_seconds=arguments.ttl,
    )
    try:
        server.serve_forever(arguments.bind, arguments.port)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
