#!/usr/bin/env python3
"""Independent source-owned positives/negatives for original byte-range policy.

No Unicorn, host-native guest entry, Sony modules or imported executables.
"""
import unittest

from owned_guest_byte_access_policy import (
    EXECUTE, READ, WRITE, U64_LIMIT, GuestRegion, GuestRegionPolicy,
    require_checked_access,
)


class GuestByteRangePolicyTests(unittest.TestCase):
    def setUp(self):
        # Two *different original virtual permissions on one 4-KiB page*.
        # The same host page can be represented as RWX storage by Unicorn,
        # but the policy must still deny X in data and W in text.
        self.policy = GuestRegionPolicy([
            GuestRegion(0x1000, 0x1010, READ | EXECUTE, "source_text"),
            GuestRegion(0x1010, 0x1020, READ | WRITE, "source_data"),
            GuestRegion(0x2000, 0x2010, READ, "source_readonly"),
            GuestRegion(0x3000, 0x3010, READ | WRITE, "data_1"),
            GuestRegion(0x3010, 0x3020, READ | WRITE, "data_2"),
        ])

    def test_exact_guest_execute_in_source_text(self):
        self.assertTrue(self.policy.check("execute", 0x1000, 5).allowed)
        self.assertTrue(self.policy.check("execute", 0x100e, 2).allowed)

    def test_no_guest_execution_in_same_page_source_data(self):
        self.assertEqual(self.policy.check("execute", 0x1010, 2).reason,
                         "permission_denied")

    def test_no_guest_write_to_same_page_source_text(self):
        self.assertEqual(self.policy.check("write", 0x1008, 8).reason,
                         "permission_denied")

    def test_read_across_distinct_original_regions_when_all_readable(self):
        d = self.policy.check("read", 0x100f, 2)
        self.assertTrue(d.allowed)
        self.assertTrue(d.crossed_region_boundary)

    def test_guest_write_spanning_rx_then_rw_denied_without_prefix(self):
        self.assertEqual(self.policy.check("write", 0x100f, 2).reason,
                         "permission_denied")

    def test_adjacent_guest_writable_regions_allow_full_crossing(self):
        d = self.policy.check("write", 0x300e, 4)
        self.assertTrue(d.allowed)
        self.assertTrue(d.crossed_region_boundary)

    def test_unmapped_gap_denied_even_if_next_mapping_writable(self):
        self.assertEqual(self.policy.check("write", 0x2010, 3).reason,
                         "unmapped_guest_byte_gap")
        self.assertEqual(self.policy.check("read", 0x200f, 3).reason,
                         "unmapped_guest_byte_gap")

    def test_readonly_guest_source_write_is_denied(self):
        self.assertEqual(self.policy.check("write", 0x2000, 1).reason,
                         "permission_denied")

    def test_mixed_host_page_census_not_permission_union(self):
        self.assertEqual(self.policy.mixed_permission_pages(),
                         frozenset({0x1000}))
        self.assertFalse(self.policy.check("execute", 0x1010, 2).allowed)
        self.assertFalse(self.policy.check("write", 0x1000, 2).allowed)

    def test_no_implicit_host_page_roundup_to_guest_extent(self):
        self.assertEqual(self.policy.check("read", 0x1020, 1).reason,
                         "unmapped_guest_byte_gap")

    def test_invalid_kinds_lengths_overflows_fail_closed(self):
        cases = (
            ("read", 0x1010, 0, "invalid_guest_access_size"),
            ("write", 0x1010, 65, "invalid_guest_access_size"),
            ("execute", 0x1000, 16, "invalid_guest_access_size"),
            ("execute", 0x1000, -1, "invalid_guest_access_size"),
            ("bogus", 0x1010, 1, "unknown_guest_access_kind"),
            ("read", U64_LIMIT, 1, "invalid_guest_address"),
            ("read", -1, 1, "invalid_guest_address"),
            ("write", U64_LIMIT - 4, 8, "guest_address_overflow"),
        )
        for kind, addr, size, reason in cases:
            with self.subTest(reason=reason, kind=kind, addr=addr):
                self.assertEqual(self.policy.check(kind, addr, size).reason,
                                 reason)

    def test_duplicate_or_overlapping_guest_mapping_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlapping"):
            GuestRegionPolicy([
                GuestRegion(0x1000, 0x1010, READ),
                GuestRegion(0x100f, 0x1020, WRITE),
            ])

    def test_invalid_mapping_permissions_rejected(self):
        with self.assertRaisesRegex(ValueError, "invalid_guest_region"):
            GuestRegionPolicy([GuestRegion(0x1000, 0x1010, 8)])

    def test_original_zero_permission_mapping_denies_every_access(self):
        policy = GuestRegionPolicy([
            GuestRegion(0x1000, 0x1010, READ | EXECUTE),
            GuestRegion(0x1010, 0x1020, 0, "source_pt_load_no_access"),
        ])
        for kind in ("read", "write", "execute"):
            self.assertEqual(policy.check(kind, 0x1010, 1).reason,
                             "permission_denied")
        # It shares a host page with executable source text but the
        # backing-page union is not guest execution/read/write authority.
        self.assertEqual(policy.mixed_permission_pages(),
                         frozenset({0x1000}))

    def test_immutable_sorted_original_extents(self):
        self.assertEqual(self.policy.regions[0].start, 0x1000)
        self.assertEqual(self.policy.regions[-1].start, 0x3010)
        with self.assertRaises(Exception):
            self.policy.regions[0].flags = READ | WRITE

    def test_explicit_raise_on_non_authorized_access(self):
        self.assertTrue(require_checked_access(
            self.policy, "write", 0x1010, 8).allowed)
        with self.assertRaisesRegex(ValueError, "guest_access_refused"):
            require_checked_access(self.policy, "write", 0x1008, 8)

    def test_end_of_address_space_is_not_wrapped(self):
        p = GuestRegionPolicy([
            GuestRegion(U64_LIMIT - 16, U64_LIMIT, READ | WRITE)
        ])
        self.assertTrue(p.check("write", U64_LIMIT - 8, 8).allowed)
        self.assertEqual(p.check("write", U64_LIMIT - 8, 9).reason,
                         "guest_address_overflow")

    def test_loader_region_dictionary_adapter(self):
        p = GuestRegionPolicy.from_loads([
            {"va": 0x1000, "end": 0x1010, "flags": READ | EXECUTE},
            {"va": 0x1010, "end": 0x1020, "flags": READ | WRITE},
        ])
        self.assertEqual(p.mixed_permission_pages(), frozenset({0x1000}))
        self.assertTrue(p.check("write", 0x1010, 8).allowed)
        self.assertFalse(p.check("write", 0x1008, 8).allowed)


if __name__ == "__main__":
    import sys
    outcome = unittest.TextTestRunner(verbosity=1).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(
            GuestByteRangePolicyTests))
    if not outcome.wasSuccessful():
        sys.exit(1)
    print("C1 ORIGINAL GUEST BYTE-RANGE POLICY PASS")
    print(f"source_owned_policy_cases={outcome.testsRun}")
    print("mixed_host_page_guest_RX_RW_separation=checked")
    print("cross_region_and_gap_access=checked")
    print("overflow_and_invalid_access=refused")
    print("zero_permission_source_mapping=all_access_refused")
    print("no_native_guest_execution=confirmed")
