#!/usr/bin/env python3
"""Research-only original guest-byte-range access planner.

Unicorn's virtual memory and its permissions are page-granular. Original
ELF PT_LOAD extents can be smaller than one 4-KiB page, even with different
read/write/execute permissions on disjoint portions of that same page.

This reusable *policy oracle* checks the original exact byte ranges, not
the union permissions used by the isolated research engine for storage.
It neither registers CPU hooks nor guarantees that a pre-write hook can
prevent an already-started instruction from mutating Unicorn memory.
Only an independently bounded, source-owned instruction path plus full
post-execution guest-memory verification can qualify a research outcome.
It is NOT a hostile-code sandbox, production MMU, or Sony runtime.
"""
from dataclasses import dataclass

U64_LIMIT = 1 << 64
PAGE_BYTES = 4096
READ = 4
WRITE = 2
EXECUTE = 1
MAX_REGIONS = 64
MAX_ACCESS_BYTES = 64
MAX_INSTRUCTION_BYTES = 15


@dataclass(frozen=True)
class GuestRegion:
    start: int
    end: int                       # exclusive; 2**64 is a valid terminal end
    flags: int                     # original ELF R=4, W=2, X=1
    name: str = "source"


@dataclass(frozen=True)
class GuestAccessDecision:
    allowed: bool
    reason: str                    # allowed/unmapped/permission_denied/etc.
    address: int
    size: int
    kind: str
    crossed_region_boundary: bool = False


class GuestRegionPolicy:
    """Immutable, finite source-mapping authority; no host pointers.

    Adjacent original regions may be traversed by a single access only when
    *every byte* is covered by regions permitting the requested operation.
    Unmapped gaps and permission breaks are denied as a complete request.
    Nothing here copies or mutates guest memory.
    """

    def __init__(self, regions):
        incoming = tuple(regions)
        if not incoming or len(incoming) > MAX_REGIONS:
            raise ValueError("invalid_guest_region_count")
        ordered = sorted(incoming, key=lambda r: (r.start, r.end))
        previous_end = -1
        for region in ordered:
            if not isinstance(region, GuestRegion):
                raise ValueError("invalid_guest_region_type")
            if (not 0 <= region.start < region.end <= U64_LIMIT or
                    not 0 <= region.flags <= 7 or region.flags == 0 or
                    not isinstance(region.name, str) or
                    not region.name or len(region.name) > 64):
                raise ValueError("invalid_guest_region_extent_or_permissions")
            if region.start < previous_end:
                raise ValueError("overlapping_original_guest_regions")
            previous_end = region.end
        self._regions = tuple(ordered)

    @classmethod
    def from_loads(cls, source_regions):
        """Consume checked original PT_LOAD intents, never page unions."""
        return cls(GuestRegion(
            start=region["va"], end=region["end"],
            flags=region["flags"], name="source_pt_load"
        ) for region in source_regions)

    @property
    def regions(self):
        return self._regions

    def check(self, kind, address, size):
        flag = {"read": READ, "write": WRITE, "execute": EXECUTE}.get(kind)
        if flag is None:
            return GuestAccessDecision(False, "unknown_guest_access_kind",
                                       address, size, kind)
        if (not isinstance(address, int) or isinstance(address, bool) or
                not 0 <= address < U64_LIMIT):
            return GuestAccessDecision(False, "invalid_guest_address",
                                       address, size, kind)
        maximum = (MAX_INSTRUCTION_BYTES if kind == "execute"
                   else MAX_ACCESS_BYTES)
        if (not isinstance(size, int) or isinstance(size, bool) or
                not 0 < size <= maximum):
            return GuestAccessDecision(False, "invalid_guest_access_size",
                                       address, size, kind)
        if size > U64_LIMIT - address:
            return GuestAccessDecision(False, "guest_address_overflow",
                                       address, size, kind)

        cursor = address
        end = address + size
        traversed = 0
        for region in self._regions:
            if region.end <= cursor:
                continue
            if region.start > cursor:
                return GuestAccessDecision(False, "unmapped_guest_byte_gap",
                                           address, size, kind)
            if (region.flags & flag) == 0:
                return GuestAccessDecision(False, "permission_denied",
                                           address, size, kind)
            cursor = min(end, region.end)
            traversed += 1
            if cursor == end:
                return GuestAccessDecision(True, "allowed", address, size,
                                           kind, traversed > 1)
        return GuestAccessDecision(False, "unmapped_guest_byte_gap",
                                   address, size, kind)

    def mixed_permission_pages(self):
        """Source-page collisions are evidence, never automatic authority."""
        pages = {}
        mixed = set()
        for region in self._regions:
            lo = region.start // PAGE_BYTES * PAGE_BYTES
            high = ((region.end - 1) // PAGE_BYTES) * PAGE_BYTES
            for page in range(lo, high + 1, PAGE_BYTES):
                prior = pages.get(page)
                if prior is not None and prior != region.flags:
                    mixed.add(page)
                pages[page] = region.flags if prior is None else prior | region.flags
        return frozenset(mixed)


def require_checked_access(policy, kind, address, size):
    """Explicit failure usable by source-owned evaluators only."""
    result = policy.check(kind, address, size)
    if not result.allowed:
        raise ValueError(
            "guest_access_refused:" + result.reason +
            f":{kind}:{address!r}:{size!r}")
    return result
