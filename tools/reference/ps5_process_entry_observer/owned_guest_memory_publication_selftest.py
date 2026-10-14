#!/usr/bin/env python3
"""Research publication-boundary regression: positive and refused outcomes."""
import unittest

from owned_guest_byte_access_policy import (
    GuestRegion, GuestRegionPolicy, READ, WRITE, EXECUTE, PAGE_BYTES,
)
from owned_guest_memory_publication import (
    GuestMemoryPublication, ProposedGuestWrite, PublishedGuestMemory,
)

A = 0x1000
B = 0x2000
C = 0x3000


def pages():
    return {A: bytes(PAGE_BYTES), B: bytes(PAGE_BYTES), C: bytes(PAGE_BYTES)}


def policy():
    return GuestRegionPolicy([
        GuestRegion(A, A + 0x100, READ | EXECUTE),
        GuestRegion(A + 0x100, A + 0x200, READ | WRITE),
        GuestRegion(B, B + PAGE_BYTES, READ | WRITE),
        GuestRegion(C, C + 0x10, READ | WRITE),
        GuestRegion(C + 0x10, C + 0x20, READ | WRITE),
        GuestRegion(C + 0x20, C + 0x40, 0),
    ])


def changed(before, address, payload):
    after = dict(before)
    for i, value in enumerate(payload):
        location = address + i
        page = location // PAGE_BYTES * PAGE_BYTES
        data = bytearray(after[page])
        data[location - page] = value
        after[page] = bytes(data)
    return after


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.original = pages()
        self.policy = policy()

    def test_one_valid_write_and_detached_publish(self):
        after = changed(self.original, A + 0x100, b"\x2a" * 8)
        tx = GuestMemoryPublication(self.policy, self.original)
        self.assertTrue(tx.verify(
            after, [ProposedGuestWrite(A + 0x100, b"\x2a" * 8)],
            "typed_invalid_instruction", terminal_attested=True))
        published = tx.publish()
        self.assertIsInstance(published, PublishedGuestMemory)
        self.assertEqual(published.read(A + 0x100, 8), b"\x2a" * 8)
        after[A] = bytes(PAGE_BYTES)
        self.assertEqual(published.read(A + 0x100, 8), b"\x2a" * 8)
        self.assertEqual(self.original[A], bytes(PAGE_BYTES))
        self.assertEqual(published.write_count, 1)
        self.assertEqual(tx.phase, "published")

    def test_multiple_ordered_effects_are_applied(self):
        events = [
            ProposedGuestWrite(A + 0x100, b"ABCD"),
            ProposedGuestWrite(A + 0x101, b"xy"),
            ProposedGuestWrite(B + 2, b"\x31\x32"),
        ]
        final = dict(self.original)
        for event in events:
            final = changed(final, event.address, event.payload)
        tx = GuestMemoryPublication(self.policy, self.original)
        self.assertTrue(tx.verify(
            final, events, "retired", terminal_attested=True))
        out = tx.publish()
        self.assertEqual(out.read(A + 0x100, 4), b"AxyD")
        self.assertEqual(out.read(B + 2, 2), b"12")
        self.assertEqual(out.write_count, 3)

    def test_legal_cross_page_guest_write(self):
        p = GuestRegionPolicy([
            GuestRegion(A + PAGE_BYTES - 4, B + 4, READ | WRITE)])
        old = {A: bytes(PAGE_BYTES), B: bytes(PAGE_BYTES)}
        payload = b"12345678"
        tx = GuestMemoryPublication(p, old)
        after = changed(old, B - 4, payload)
        self.assertTrue(tx.verify(
            after, [ProposedGuestWrite(B - 4, payload)],
            "retired", terminal_attested=True))
        self.assertEqual(tx.publish().read(B - 4, 8), payload)

    def test_refuse_write_into_rx_with_mixed_page(self):
        self.refused(self.original, [
            ProposedGuestWrite(A + 0x20, b"X")])

    def test_refuse_write_across_rx_rw_boundary(self):
        self.refused(self.original, [
            ProposedGuestWrite(A + 0xff, b"XY")])

    def test_refuse_write_into_no_access_source(self):
        self.refused(self.original, [
            ProposedGuestWrite(C + 0x20, b"Z")])

    def test_refuse_write_across_rw_no_access_boundary(self):
        self.refused(self.original, [
            ProposedGuestWrite(C + 0x1f, b"XY")])

    def test_refuse_unmapped_gap(self):
        self.refused(self.original, [
            ProposedGuestWrite(A + 0x201, b"Q")])

    def test_refuse_incorrect_payload_even_if_actual_bytes_were_committed(self):
        after = changed(self.original, B + 12, b"Z")
        self.refused(after, [ProposedGuestWrite(B + 12, b"Y")])

    def test_refuse_hook_only_effect_without_actual_mutation(self):
        self.refused(self.original, [
            ProposedGuestWrite(B + 12, b"Z")])

    def test_refuse_undeclared_unexpected_source_mutation(self):
        altered = changed(self.original, A + 3, b"X")
        self.refused(altered, [])

    def test_refuse_additional_mutation_outside_declared_write(self):
        altered = changed(self.original, B + 12, b"Z")
        altered = changed(altered, A + 0x10, b"X")
        self.refused(altered, [ProposedGuestWrite(B + 12, b"Z")])

    def test_refuse_mapped_page_removed(self):
        altered = dict(self.original)
        del altered[C]
        self.refused(altered, [])

    def test_refuse_unexpected_host_union_page(self):
        altered = dict(self.original)
        altered[0x4000] = bytes(PAGE_BYTES)
        self.refused(altered, [])

    def test_refuse_unattested_terminal(self):
        tx = GuestMemoryPublication(self.policy, self.original)
        with self.assertRaisesRegex(ValueError, "unverified_execution_terminal"):
            tx.verify(self.original, [], "retired")
        self.assertEqual(tx.phase, "poisoned")

    def test_refuse_unqualified_partial_fault_even_with_prefix(self):
        after = changed(self.original, B + 1, b"Z")
        tx = GuestMemoryPublication(self.policy, self.original)
        with self.assertRaisesRegex(ValueError, "unqualified_partial_fault"):
            tx.verify(after, [ProposedGuestWrite(B + 1, b"Z")],
                      "fault_after_effects", terminal_attested=True)
        with self.assertRaisesRegex(ValueError, "no_verified"):
            tx.publish()

    def test_refuse_unknown_terminal(self):
        tx = GuestMemoryPublication(self.policy, self.original)
        with self.assertRaisesRegex(ValueError, "unsupported_terminal"):
            tx.verify(self.original, [], "unreviewed_guest_success",
                      terminal_attested=True)

    def test_refuse_overlong_write_event(self):
        self.refused(self.original, [
            ProposedGuestWrite(B, b"Z" * 65)])

    def test_refuse_excessive_write_events(self):
        self.refused(self.original, [
            ProposedGuestWrite(B, b"A") for _ in range(65)])

    def test_refuse_zero_write_width(self):
        self.refused(self.original, [ProposedGuestWrite(B, b"")])

    def test_refuse_non_byte_payload(self):
        self.refused(self.original, [ProposedGuestWrite(B, bytearray(b"X"))])

    def test_refuse_invalid_storage_length(self):
        tx = GuestMemoryPublication(self.policy, self.original)
        damaged = dict(self.original)
        damaged[A] = b"X"
        with self.assertRaisesRegex(ValueError, "private_guest_page_inventory"):
            tx.verify(damaged, [], "retired", terminal_attested=True)

    def test_refuse_invalid_page_source(self):
        with self.assertRaisesRegex(ValueError, "invalid_guest_page"):
            GuestMemoryPublication(self.policy, {A + 1: bytes(PAGE_BYTES)})

    def test_publish_requires_prior_verified_state_and_no_replay(self):
        tx = GuestMemoryPublication(self.policy, self.original)
        with self.assertRaisesRegex(ValueError, "no_verified"):
            tx.publish()
        tx2 = GuestMemoryPublication(self.policy, self.original)
        self.assertTrue(tx2.verify(self.original, [], "retired",
                                   terminal_attested=True))
        tx2.publish()
        with self.assertRaisesRegex(ValueError, "no_verified"):
            tx2.publish()

    def test_verified_snapshot_not_exposed_until_publish(self):
        tx = GuestMemoryPublication(self.policy, self.original)
        self.assertTrue(tx.verify(self.original, [], "retired",
                                  terminal_attested=True))
        self.assertEqual(tx.phase, "verified")
        published = tx.publish()
        self.assertEqual(published.read(B, 1), b"\x00")
        self.assertEqual(published.write_count, 0)

    def refused(self, after, writes):
        tx = GuestMemoryPublication(self.policy, self.original)
        with self.assertRaisesRegex(ValueError, "guest_publication_refused"):
            tx.verify(after, writes, "retired", terminal_attested=True)
        self.assertEqual(tx.phase, "poisoned")
        with self.assertRaisesRegex(ValueError, "no_verified"):
            tx.publish()


if __name__ == "__main__":
    import sys
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PublicationTests)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    if not result.wasSuccessful():
        sys.exit(1)
    print("C1 GUEST MEMORY DETACHED PUBLICATION PASS")
    print(f"source_owned_publication_cases={result.testsRun}")
    print("full_image_effect_validation=required")
    print("unauthorized_guest_effects=discarded")
    print("partial_fault_publication=refused_pending_restart_state")
    print("published_guest_pages=detached_immutable_copy")
    print("host_native_guest_execution=none")
