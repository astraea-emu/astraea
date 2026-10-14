#!/usr/bin/env python3
"""Bounded, fail-closed guest-memory PUBLICATION boundary (research only).

An external mediated engine is permitted to mutate its own private memory
during an instruction. This class does NOT stop or isolate those mutations.
It allows a trusted controller to accept *independent immutable copies* of
the final guest pages ONLY if each observed write is permitted by original
guest byte-range permissions, the COMPLETE final page image agrees with the
ordered write events, and a separately checked terminal outcome is attested.

Events are proposed effects, not proof of post-commit provenance; comparing
all private bytes to an expected image detects unreported mutations but does
not prove which instruction caused an equal-byte write. Not a general CPU
MMU, write barrier, hostile guest sandbox or Sony ABI.
"""
from dataclasses import dataclass
from typing import Mapping

from owned_guest_byte_access_policy import (
    GuestRegionPolicy, PAGE_BYTES, MAX_ACCESS_BYTES, U64_LIMIT,
)

MAX_PAGES = 256
MAX_EVENTS = 64
# A controlled terminal exception may be published ONLY with explicit
# trusted-caller evidence. Partial faults are refused by default.
SUPPORTED_TERMINALS = frozenset(("retired", "typed_invalid_instruction",
                                 "fault_after_effects"))


@dataclass(frozen=True)
class ProposedGuestWrite:
    address: int
    payload: bytes


@dataclass(frozen=True)
class PublishedGuestMemory:
    """Detached, immutable page contents, never a handle to emulator pages."""
    pages: tuple[tuple[int, bytes], ...]
    terminal_kind: str
    write_count: int

    def read(self, address: int, size: int) -> bytes:
        if not isinstance(address, int) or not 0 <= address < U64_LIMIT:
            raise ValueError("published_guest_address_invalid")
        if not isinstance(size, int) or not 0 < size <= MAX_ACCESS_BYTES:
            raise ValueError("published_guest_read_width_invalid")
        if size > U64_LIMIT - address:
            raise ValueError("published_guest_read_overflow")
        current = address
        out = bytearray()
        while current < address + size:
            base = current // PAGE_BYTES * PAGE_BYTES
            found = next((p for p in self.pages if p[0] == base), None)
            if found is None:
                raise ValueError("published_guest_page_missing")
            offset = current - base
            take = min(PAGE_BYTES - offset, address + size - current)
            out.extend(found[1][offset:offset + take])
            current += take
        return bytes(out)


class GuestMemoryPublication:
    def __init__(self, policy: GuestRegionPolicy, initial_pages: Mapping[int, bytes]):
        if not isinstance(policy, GuestRegionPolicy):
            raise ValueError("original_guest_permission_policy_required")
        self._phase = "invalid"
        self._policy = policy
        self._verified = None
        self._initial = self._copy_pages(initial_pages)
        self._phase = "recording"

    @staticmethod
    def _copy_pages(source):
        if not isinstance(source, Mapping) or not 0 < len(source) <= MAX_PAGES:
            raise ValueError("invalid_guest_page_inventory")
        result = {}
        for base, contents in source.items():
            if (type(base) is not int or base < 0 or
                    base >= U64_LIMIT or base % PAGE_BYTES or
                    base > U64_LIMIT - PAGE_BYTES or
                    type(contents) is not bytes or len(contents) != PAGE_BYTES):
                raise ValueError("invalid_guest_page_address_or_contents")
            result[base] = bytes(contents)
        return result

    def _refuse(self, reason):
        self._phase = "poisoned"
        self._verified = None
        raise ValueError("guest_publication_refused:" + reason)

    def verify(self, private_pages, proposed_writes, terminal_kind,
               *, terminal_attested=False, permit_partial_fault=False):
        if self._phase != "recording":
            self._refuse("transaction_not_recording")
        if terminal_kind not in SUPPORTED_TERMINALS:
            self._refuse("unsupported_terminal_kind")
        if type(terminal_attested) is not bool or not terminal_attested:
            self._refuse("unverified_execution_terminal")
        if terminal_kind == "fault_after_effects" and not permit_partial_fault:
            self._refuse("unqualified_partial_fault")
        if type(permit_partial_fault) is not bool:
            self._refuse("invalid_partial_fault_authorization")
        if not isinstance(proposed_writes, (list, tuple)) or (
                len(proposed_writes) > MAX_EVENTS):
            self._refuse("unbounded_effect_ledger")
        if terminal_kind == "fault_after_effects" and not proposed_writes:
            self._refuse("partial_fault_without_committed_prefix")

        expected = {page: bytearray(data)
                    for page, data in self._initial.items()}
        for effect in proposed_writes:
            if not isinstance(effect, ProposedGuestWrite):
                self._refuse("malformed_effect_record")
            addr, payload = effect.address, effect.payload
            if type(payload) is not bytes or not 0 < len(payload) <= MAX_ACCESS_BYTES:
                self._refuse("effect_width_invalid")
            decision = self._policy.check("write", addr, len(payload))
            if not decision.allowed:
                self._refuse("original_guest_permission_" + decision.reason)
            current = addr
            for value in payload:
                base = current // PAGE_BYTES * PAGE_BYTES
                if base not in expected:
                    self._refuse("effect_guest_storage_unmapped")
                expected[base][current - base] = value
                current += 1

        try:
            actual = self._copy_pages(private_pages)
        except (TypeError, ValueError):
            self._refuse("private_guest_page_inventory_invalid")
        if actual.keys() != expected.keys():
            self._refuse("private_guest_mapping_set_changed")
        if any(actual[page] != expected[page] for page in expected):
            self._refuse("unreported_or_divergent_private_guest_mutation")

        self._verified = PublishedGuestMemory(
            pages=tuple(sorted(
                (base, bytes(data)) for base, data in expected.items())),
            terminal_kind=terminal_kind,
            write_count=len(proposed_writes),
        )
        self._phase = "verified"
        return self._verified

    def publish(self):
        if self._phase != "verified" or self._verified is None:
            self._refuse("no_verified_guest_image_to_publish")
        published = self._verified
        self._phase = "published"
        self._verified = None
        return published

    @property
    def phase(self):
        return self._phase
