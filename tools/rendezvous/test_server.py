from __future__ import annotations

import socket
import struct
import threading
import unittest

from tools.rendezvous import protocol
from tools.rendezvous import fake_stun
from tools.rendezvous.server import RendezvousServer

SECRET = b"test rendezvous secret material!!"
HOST_ADDRESS = ("198.51.100.10", 40000)
CLIENT_ADDRESS = ("203.0.113.20", 41000)
SECOND_CLIENT_ADDRESS = ("192.0.2.30", 42000)


def packet(message_type: protocol.MessageType, payload: bytes = b"", transaction_id: int = 1) -> bytes:
    return protocol.encode_packet(message_type, transaction_id, payload)


def decoded_only(responses: list[tuple[bytes, tuple[str, int]]]) -> protocol.Packet:
    return protocol.decode_packet(responses[0][0])


class RendezvousServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.server = RendezvousServer(SECRET, ttl_seconds=30, rate=10, burst=20)
        self.now = 1000.0

    def cookie(self, address: tuple[str, int], transaction_id: int = 1) -> bytes:
        response = self.server.process_datagram(
            packet(protocol.MessageType.COOKIE_REQUEST, b"N" * 16, transaction_id),
            address,
            now=self.now,
        )
        challenge = decoded_only(response)
        self.assertEqual(challenge.message_type, protocol.MessageType.COOKIE_CHALLENGE)
        self.assertEqual(len(response[0][0]), len(packet(protocol.MessageType.COOKIE_REQUEST, b"N" * 16)))
        return challenge.payload

    def create(
        self, address: tuple[str, int] = HOST_ADDRESS
    ) -> tuple[bytes, bytes, int, int]:
        cookie = self.cookie(address)
        response = self.server.process_datagram(
            packet(protocol.MessageType.CREATE, cookie + protocol.encode_candidates([]), 2),
            address,
            now=self.now,
        )
        created = decoded_only(response)
        self.assertEqual(created.message_type, protocol.MessageType.CREATED)
        room = created.payload[:8]
        token = created.payload[8:24]
        peer_id, epoch = struct.unpack("!II", created.payload[24:])
        return room, token, peer_id, epoch

    def join(
        self, room: bytes, address: tuple[str, int] = CLIENT_ADDRESS
    ) -> tuple[bytes, int, int, list[tuple[bytes, tuple[str, int]]]]:
        cookie = self.cookie(address, 3)
        candidates = [protocol.Candidate(protocol.CandidateType.LOCAL, 0x0A000002, 5000)]
        responses = self.server.process_datagram(
            packet(
                protocol.MessageType.JOIN,
                cookie + room + protocol.encode_candidates(candidates),
                4,
            ),
            address,
            now=self.now,
        )
        joined = protocol.decode_packet(responses[0][0])
        self.assertEqual(joined.message_type, protocol.MessageType.JOINED)
        token = joined.payload[:16]
        peer_id, epoch, host_peer_id = struct.unpack("!III", joined.payload[16:28])
        self.assertEqual(host_peer_id, 1)
        host_candidates = protocol.decode_candidates(joined.payload[28:])
        self.assertEqual(host_candidates[-1].candidate_type, protocol.CandidateType.PEER_REFLEXIVE)
        return token, peer_id, epoch, responses

    def test_create_join_and_candidate_notification(self) -> None:
        room, _, host_peer_id, epoch = self.create()
        self.assertEqual(host_peer_id, 1)
        self.assertEqual(epoch, 1)
        self.assertEqual(len(room), 8)
        self.assertTrue(set(room.decode()).issubset(set("0123456789ABCDEFGHJKMNPQRSTVWXYZ")))

        token, peer_id, joined_epoch, responses = self.join(room)
        self.assertEqual((peer_id, joined_epoch), (2, 1))
        self.assertEqual(responses[1][1], HOST_ADDRESS)
        notice = protocol.decode_packet(responses[1][0])
        self.assertEqual(notice.message_type, protocol.MessageType.PEER_JOINED)
        self.assertEqual(struct.unpack("!I", notice.payload[:4])[0], 2)
        self.assertEqual(notice.payload[4:20], token)
        candidates = protocol.decode_candidates(notice.payload[20:])
        self.assertEqual(candidates[-1].host, int.from_bytes(socket.inet_aton(CLIENT_ADDRESS[0]), "big"))
        self.assertEqual(candidates[-1].port, CLIENT_ADDRESS[1])

    def test_refresh_update_leave_and_rejected_token(self) -> None:
        room, _, _, _ = self.create()
        token, _, _, _ = self.join(room)
        cookie = self.cookie(CLIENT_ADDRESS, 5)

        refresh = self.server.process_datagram(
            packet(protocol.MessageType.REFRESH, cookie + token, 6),
            CLIENT_ADDRESS,
            now=self.now + 1,
        )
        self.assertEqual(decoded_only(refresh).payload, struct.pack("!I", 1))

        new_candidates = protocol.encode_candidates(
            [protocol.Candidate(protocol.CandidateType.SERVER_REFLEXIVE, 0xCB007114, 6000)]
        )
        update = self.server.process_datagram(
            packet(protocol.MessageType.CANDIDATE_UPDATE, cookie + token + new_candidates, 7),
            CLIENT_ADDRESS,
            now=self.now + 2,
        )
        self.assertEqual(protocol.decode_packet(update[0][0]).message_type, protocol.MessageType.CANDIDATE_UPDATE)
        self.assertEqual(protocol.decode_packet(update[1][0]).message_type, protocol.MessageType.PEER_JOINED)

        rejected = self.server.process_datagram(
            packet(protocol.MessageType.REFRESH, cookie + b"X" * 16, 8),
            CLIENT_ADDRESS,
            now=self.now + 3,
        )
        self.assertEqual(decoded_only(rejected).payload, bytes((protocol.ErrorCode.ADMISSION_REJECTED,)))

        leave = self.server.process_datagram(
            packet(protocol.MessageType.LEAVE, cookie + token, 9),
            CLIENT_ADDRESS,
            now=self.now + 4,
        )
        self.assertEqual(decoded_only(leave).message_type, protocol.MessageType.LEAVE)

    def test_expiry_and_deterministic_host_promotion(self) -> None:
        room, host_token, _, _ = self.create()
        first_token, _, epoch, _ = self.join(room, CLIENT_ADDRESS)
        second_token, _, _, _ = self.join(room, SECOND_CLIENT_ADDRESS)

        first_cookie = self.cookie(CLIENT_ADDRESS, 10)
        second_cookie = self.cookie(SECOND_CLIENT_ADDRESS, 11)
        self.server.process_datagram(
            packet(protocol.MessageType.REFRESH, first_cookie + first_token, 12),
            CLIENT_ADDRESS,
            now=self.now + 20,
        )
        self.server.process_datagram(
            packet(protocol.MessageType.REFRESH, second_cookie + second_token, 13),
            SECOND_CLIENT_ADDRESS,
            now=self.now + 20,
        )

        promote = self.server.process_datagram(
            packet(
                protocol.MessageType.PROMOTE,
                first_cookie + first_token + struct.pack("!I", epoch),
                14,
            ),
            CLIENT_ADDRESS,
            now=self.now + 31,
        )
        self.assertNotIn(host_token, self.server.rooms[room].participants)
        self.assertEqual(protocol.decode_packet(promote[0][0]).message_type, protocol.MessageType.PROMOTE)
        self.assertEqual(protocol.decode_packet(promote[0][0]).payload, struct.pack("!I", 2))
        self.assertEqual(self.server.rooms[room].host_token, first_token)
        snapshots_to_new_host = [
            protocol.decode_packet(data)
            for data, destination in promote[1:]
            if destination == CLIENT_ADDRESS
            and protocol.decode_packet(data).message_type == protocol.MessageType.PEER_JOINED
        ]
        self.assertEqual(len(snapshots_to_new_host), 1)
        self.assertEqual(snapshots_to_new_host[0].payload[4:20], second_token)
        promoted_candidates = protocol.decode_candidates(snapshots_to_new_host[0].payload[20:])
        self.assertEqual(promoted_candidates[-1].port, SECOND_CLIENT_ADDRESS[1])

        conflict = self.server.process_datagram(
            packet(
                protocol.MessageType.PROMOTE,
                second_cookie + second_token + struct.pack("!I", 2),
                15,
            ),
            SECOND_CLIENT_ADDRESS,
            now=self.now + 31,
        )
        self.assertEqual(decoded_only(conflict).payload, bytes((protocol.ErrorCode.CONFLICT,)))

    def test_promotion_accepts_stale_host_before_room_ttl(self) -> None:
        room, host_token, _, _ = self.create()
        client_token, _, epoch, _ = self.join(room, CLIENT_ADDRESS)
        client_cookie = self.cookie(CLIENT_ADDRESS, 20)

        fresh_conflict = self.server.process_datagram(
            packet(
                protocol.MessageType.PROMOTE,
                client_cookie + client_token + struct.pack("!I", epoch),
                21,
            ),
            CLIENT_ADDRESS,
            now=self.now + 5,
        )
        self.assertEqual(
            decoded_only(fresh_conflict).payload,
            bytes((protocol.ErrorCode.CONFLICT,)),
        )

        promoted = self.server.process_datagram(
            packet(
                protocol.MessageType.PROMOTE,
                client_cookie + client_token + struct.pack("!I", epoch),
                22,
            ),
            CLIENT_ADDRESS,
            now=self.now + 11,
        )
        self.assertEqual(decoded_only(promoted).message_type, protocol.MessageType.PROMOTE)
        self.assertEqual(self.server.rooms[room].host_token, client_token)
        self.assertNotIn(host_token, self.server.rooms[room].participants)

    def test_join_and_candidate_updates_are_broadcast_to_existing_peers(self) -> None:
        room, _, _, _ = self.create()
        first_token, _, _, _ = self.join(room, CLIENT_ADDRESS)
        second_token, second_peer_id, _, second_join = self.join(room, SECOND_CLIENT_ADDRESS)
        notice_destinations = {
            destination
            for data, destination in second_join[1:]
            if protocol.decode_packet(data).message_type == protocol.MessageType.PEER_JOINED
            and struct.unpack("!I", protocol.decode_packet(data).payload[:4])[0] == second_peer_id
        }
        self.assertEqual(notice_destinations, {HOST_ADDRESS, CLIENT_ADDRESS})
        existing_snapshots = [
            protocol.decode_packet(data)
            for data, destination in second_join[1:]
            if destination == SECOND_CLIENT_ADDRESS
            and protocol.decode_packet(data).message_type == protocol.MessageType.PEER_JOINED
        ]
        self.assertEqual(
            {snapshot.payload[4:20] for snapshot in existing_snapshots},
            {self.server.rooms[room].host_token, first_token},
        )

        cookie = self.cookie(SECOND_CLIENT_ADDRESS, 30)
        update = self.server.process_datagram(
            packet(
                protocol.MessageType.CANDIDATE_UPDATE,
                cookie
                + second_token
                + protocol.encode_candidates(
                    [protocol.Candidate(protocol.CandidateType.LOCAL, 0x0A000003, 7000)]
                ),
                31,
            ),
            SECOND_CLIENT_ADDRESS,
            now=self.now + 1,
        )
        update_notices = [
            (protocol.decode_packet(data), destination) for data, destination in update[1:]
        ]
        self.assertEqual({destination for _, destination in update_notices}, {HOST_ADDRESS, CLIENT_ADDRESS})
        self.assertTrue(
            all(struct.unpack("!I", notice.payload[:4])[0] == second_peer_id for notice, _ in update_notices)
        )
        self.assertNotEqual(first_token, second_token)

    def test_room_full(self) -> None:
        room, _, _, _ = self.create()
        for index in range(31):
            address = (f"10.0.0.{index + 1}", 20000 + index)
            self.join(room, address)
        extra_address = ("10.0.1.1", 30000)
        cookie = self.cookie(extra_address, 20)
        response = self.server.process_datagram(
            packet(protocol.MessageType.JOIN, cookie + room + protocol.encode_candidates([]), 21),
            extra_address,
            now=self.now,
        )
        self.assertEqual(decoded_only(response).payload, bytes((protocol.ErrorCode.ROOM_FULL,)))

    def test_cookie_binding_rate_limit_and_malformed_silence(self) -> None:
        cookie = self.cookie(HOST_ADDRESS)
        wrong_source = self.server.process_datagram(
            packet(protocol.MessageType.CREATE, cookie + protocol.encode_candidates([]), 2),
            CLIENT_ADDRESS,
            now=self.now,
        )
        self.assertEqual(decoded_only(wrong_source).payload, bytes((protocol.ErrorCode.INVALID_COOKIE,)))

        stable_cookie = self.server.process_datagram(
            packet(protocol.MessageType.CREATE, cookie + protocol.encode_candidates([]), 22),
            HOST_ADDRESS,
            now=self.now + 3600,
        )
        self.assertEqual(decoded_only(stable_cookie).message_type, protocol.MessageType.CREATED)

        self.assertEqual(self.server.process_datagram(b"bad", HOST_ADDRESS, now=self.now), [])
        oversized = b"X" * (protocol.MAX_DATAGRAM_SIZE + 1)
        self.assertEqual(self.server.process_datagram(oversized, HOST_ADDRESS, now=self.now), [])

        limited_server = RendezvousServer(SECRET, rate=1, burst=1)
        request = packet(protocol.MessageType.COOKIE_REQUEST, b"N" * 16)
        self.assertEqual(
            decoded_only(limited_server.process_datagram(request, HOST_ADDRESS, now=0)).message_type,
            protocol.MessageType.COOKIE_CHALLENGE,
        )
        limited = limited_server.process_datagram(request, HOST_ADDRESS, now=0)
        self.assertEqual(decoded_only(limited).payload, bytes((protocol.ErrorCode.RATE_LIMITED,)))

    def test_protocol_rejects_bad_bounds_and_types(self) -> None:
        valid = packet(protocol.MessageType.COOKIE_REQUEST, b"N" * 16)
        bad_length = bytearray(valid)
        bad_length[6:8] = struct.pack("!H", 100)
        with self.assertRaises(protocol.ProtocolError):
            protocol.decode_packet(bytes(bad_length))
        with self.assertRaises(protocol.ProtocolError):
            protocol.decode_candidates(bytes((33,)))
        with self.assertRaises(protocol.ProtocolError):
            protocol.decode_candidates(b"\x01\x03\x00\x00\x00\x00\x12\x34")


class UdpIntegrationTests(unittest.TestCase):
    def test_real_udp_cookie_exchange(self) -> None:
        server = RendezvousServer(SECRET)
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server_socket.bind(("127.0.0.1", 0))
        server_address = server_socket.getsockname()

        def serve_once() -> None:
            data, source = server_socket.recvfrom(protocol.MAX_DATAGRAM_SIZE + 1)
            for response, destination in server.process_datagram(data, source):
                server_socket.sendto(response, destination)

        thread = threading.Thread(target=serve_once)
        thread.start()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.settimeout(2)
            request = packet(protocol.MessageType.COOKIE_REQUEST, b"U" * 16, 99)
            client.sendto(request, server_address)
            response, _ = client.recvfrom(protocol.MAX_DATAGRAM_SIZE)
        thread.join(timeout=2)
        server_socket.close()
        challenge = protocol.decode_packet(response)
        self.assertEqual(challenge.transaction_id, 99)
        self.assertEqual(challenge.message_type, protocol.MessageType.COOKIE_CHALLENGE)
        self.assertEqual(len(challenge.payload), protocol.COOKIE_SIZE)


class FakeStunTests(unittest.TestCase):
    def test_binding_response_contains_observed_ipv4_endpoint(self) -> None:
        transaction_id = bytes(range(12))
        request = fake_stun.STUN_HEADER.pack(
            fake_stun.BINDING_REQUEST,
            0,
            fake_stun.MAGIC_COOKIE,
            transaction_id,
        )
        source = ("203.0.113.45", 54321)
        response = fake_stun.build_binding_response(request, source)
        self.assertIsNotNone(response)
        assert response is not None

        message_type, message_length, magic_cookie, echoed_transaction = (
            fake_stun.STUN_HEADER.unpack_from(response)
        )
        self.assertEqual(message_type, fake_stun.BINDING_SUCCESS_RESPONSE)
        self.assertEqual(message_length, 12)
        self.assertEqual(magic_cookie, fake_stun.MAGIC_COOKIE)
        self.assertEqual(echoed_transaction, transaction_id)

        attribute_type, attribute_length, reserved, family, xored_port, xored_address = (
            fake_stun.XOR_MAPPED_ATTRIBUTE.unpack_from(response, fake_stun.STUN_HEADER.size)
        )
        self.assertEqual((attribute_type, attribute_length, reserved, family), (0x0020, 8, 0, 1))
        self.assertEqual(xored_port ^ (fake_stun.MAGIC_COOKIE >> 16), source[1])
        decoded_address = bytes(
            left ^ right
            for left, right in zip(xored_address, struct.pack("!I", fake_stun.MAGIC_COOKIE))
        )
        self.assertEqual(socket.inet_ntoa(decoded_address), source[0])

    def test_malformed_binding_requests_are_silently_dropped(self) -> None:
        transaction_id = b"T" * 12
        valid = fake_stun.STUN_HEADER.pack(
            fake_stun.BINDING_REQUEST, 0, fake_stun.MAGIC_COOKIE, transaction_id
        )
        malformed = [
            b"",
            valid[:-1],
            valid + b"\x00",
            fake_stun.STUN_HEADER.pack(0x0002, 0, fake_stun.MAGIC_COOKIE, transaction_id),
            fake_stun.STUN_HEADER.pack(
                fake_stun.BINDING_REQUEST, 4, fake_stun.MAGIC_COOKIE, transaction_id
            ),
            fake_stun.STUN_HEADER.pack(fake_stun.BINDING_REQUEST, 0, 0, transaction_id),
            b"X" * (protocol.MAX_DATAGRAM_SIZE + 1),
        ]
        for request in malformed:
            with self.subTest(size=len(request), prefix=request[:4]):
                self.assertIsNone(
                    fake_stun.build_binding_response(request, ("192.0.2.1", 40000))
                )


if __name__ == "__main__":
    unittest.main()
