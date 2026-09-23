#include <astraea/graphics/pm4_write_data_ir.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <variant>
#include <vector>

#include <astraea/graphics/pm4_type3_framing.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t make_type3_header(
    std::uint8_t opcode,
    std::uint16_t encoded_count,
    std::uint8_t low_control_bits = 0) {
    return
        (static_cast<std::uint32_t>(
             astraea::graphics::kPm4Type3PacketType)
         << 30U) |
        ((static_cast<std::uint32_t>(encoded_count) &
          0x3fffU)
         << 16U) |
        (static_cast<std::uint32_t>(opcode) << 8U) |
        static_cast<std::uint32_t>(low_control_bits);
}

void append_word(
    std::vector<std::byte>& bytes,
    std::uint32_t word) {
    for (std::size_t index = 0;
         index < 4U;
         ++index) {
        bytes.push_back(
            std::byte{
                static_cast<unsigned char>(
                    (word >> (index * 8U)) &
                    0xffU)});
    }
}

std::vector<std::byte> make_stream(
    std::initializer_list<std::uint32_t> words) {
    std::vector<std::byte> bytes;
    bytes.reserve(words.size() * 4U);
    for (const auto word : words) {
        append_word(bytes, word);
    }
    return bytes;
}

astraea::graphics::Pm4Type3Frame frame_one(
    std::initializer_list<std::uint32_t> words) {
    auto framed =
        astraea::graphics::frame_pm4_type3_stream(
            make_stream(words));
    REQUIRE(framed.has_value());
    REQUIRE(framed->frames.size() == 1U);
    return std::move(framed->frames.front());
}

astraea::graphics::GraphicsIrEmission lower_one(
    std::initializer_list<std::uint32_t> words) {
    auto frame = frame_one(words);
    auto lowered =
        astraea::graphics::
            lower_pm4_write_data_frame_to_graphics_ir(
                frame);
    REQUIRE(lowered.has_value());
    return std::move(lowered).value();
}

const astraea::graphics::GraphicsIrGpuMemoryWrite&
require_memory_write(
    const astraea::graphics::GraphicsIrEmission& emission) {
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::
                GraphicsIrGpuMemoryWrite>(
            emission.operation));
    return std::get<
        astraea::graphics::GraphicsIrGpuMemoryWrite>(
        emission.operation);
}

}  // namespace

TEST_CASE(
    "WRITE_DATA one-dword Linux memory profile lowers exact address and value",
    "[graphics][pm4][write-data]") {
    constexpr std::uint64_t address =
        0x0000123456789000ULL;
    const auto emission =
        lower_one({
            make_type3_header(
                astraea::graphics::kPm4WriteDataOpcode,
                3U),
            astraea::graphics::
                kPm4WriteDataSupportedControlWord,
            static_cast<std::uint32_t>(
                address & 0xffffffffULL),
            static_cast<std::uint32_t>(
                address >> 32U),
            0xdeadbeefU,
        });

    const auto& operation =
        require_memory_write(emission);
    REQUIRE(operation.destination.value == address);
    REQUIRE(
        operation.values ==
        std::vector<std::uint32_t>{
            0xdeadbeefU});
    REQUIRE(
        emission.provenance.source_packet.extent.word_count ==
        5U);
}

TEST_CASE(
    "WRITE_DATA multi-dword payload preserves source order",
    "[graphics][pm4][write-data]") {
    constexpr std::uint64_t address =
        0xfedcba9876543000ULL;
    const auto emission =
        lower_one({
            make_type3_header(
                astraea::graphics::kPm4WriteDataOpcode,
                6U),
            astraea::graphics::
                kPm4WriteDataSupportedControlWord,
            static_cast<std::uint32_t>(
                address & 0xffffffffULL),
            static_cast<std::uint32_t>(
                address >> 32U),
            0x01020304U,
            0x11121314U,
            0xaabbccddU,
            0x55667788U,
        });

    const auto& operation =
        require_memory_write(emission);
    REQUIRE(operation.destination.value == address);
    REQUIRE(
        operation.values ==
        std::vector<std::uint32_t>{
            0x01020304U,
            0x11121314U,
            0xaabbccddU,
            0x55667788U});
}

TEST_CASE(
    "non-WRITE_DATA Type-3 opcode remains unsupported Graphics IR",
    "[graphics][pm4][write-data][unsupported]") {
    const auto emission =
        lower_one({
            make_type3_header(0x36U, 3U),
            astraea::graphics::
                kPm4WriteDataSupportedControlWord,
            0x00001000U,
            0U,
            0xdeadbeefU,
        });

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::GraphicsIrUnsupported>(
            emission.operation));
}

TEST_CASE(
    "WRITE_DATA requires at least one inline payload dword",
    "[graphics][pm4][write-data][negative]") {
    auto frame =
        frame_one({
            make_type3_header(
                astraea::graphics::kPm4WriteDataOpcode,
                2U),
            astraea::graphics::
                kPm4WriteDataSupportedControlWord,
            0x00001000U,
            0U,
        });

    const auto result =
        astraea::graphics::
            lower_pm4_write_data_frame_to_graphics_ir(
                frame);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4WriteDataLowerErrorCode::
                malformed_write_data);
    REQUIRE(result.error().word_offset == 0U);
}

TEST_CASE(
    "WRITE_DATA rejects nonzero Type3 low control bits",
    "[graphics][pm4][write-data][negative]") {
    auto frame =
        frame_one({
            make_type3_header(
                astraea::graphics::kPm4WriteDataOpcode,
                3U,
                0x01U),
            astraea::graphics::
                kPm4WriteDataSupportedControlWord,
            0x00001000U,
            0U,
            0xdeadbeefU,
        });

    const auto result =
        astraea::graphics::
            lower_pm4_write_data_frame_to_graphics_ir(
                frame);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4WriteDataLowerErrorCode::
                unsupported_type3_header_control_bits);
    REQUIRE(
        result.error().type3_header_control_bits ==
        0x01U);
}

TEST_CASE(
    "WRITE_DATA first profile rejects every unsupported control variation",
    "[graphics][pm4][write-data][control][negative]") {
    for (const auto control : {
             std::uint32_t{0x00000500U},  // no write confirm
             std::uint32_t{0x00100400U},  // different destination
             std::uint32_t{0x00110500U},  // do not increment
             std::uint32_t{0x00180500U},  // unsupported/reserved bit
             std::uint32_t{0x02100500U},  // non-LRU cache policy
             std::uint32_t{0x40100500U},  // non-ME engine
         }) {
        auto frame =
            frame_one({
                make_type3_header(
                    astraea::graphics::
                        kPm4WriteDataOpcode,
                    3U),
                control,
                0x00001000U,
                0U,
                0xdeadbeefU,
            });

        const auto result =
            astraea::graphics::
                lower_pm4_write_data_frame_to_graphics_ir(
                    frame);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Pm4WriteDataLowerErrorCode::
                    unsupported_write_control);
        REQUIRE(
            result.error().raw_control_word ==
            control);
        REQUIRE(result.error().value_count == 1U);
    }
}

TEST_CASE(
    "WRITE_DATA rejects unaligned destination address",
    "[graphics][pm4][write-data][address][negative]") {
    constexpr std::uint64_t address =
        0x0000123456789002ULL;
    auto frame =
        frame_one({
            make_type3_header(
                astraea::graphics::kPm4WriteDataOpcode,
                3U),
            astraea::graphics::
                kPm4WriteDataSupportedControlWord,
            static_cast<std::uint32_t>(
                address & 0xffffffffULL),
            static_cast<std::uint32_t>(
                address >> 32U),
            0xdeadbeefU,
        });

    const auto result =
        astraea::graphics::
            lower_pm4_write_data_frame_to_graphics_ir(
                frame);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4WriteDataLowerErrorCode::
                destination_address_unaligned);
    REQUIRE(
        result.error().destination.value ==
        address);
    REQUIRE(result.error().value_count == 1U);
}

TEST_CASE(
    "WRITE_DATA round-trips representative aligned 64-bit GPU addresses",
    "[graphics][pm4][write-data][address]") {
    for (const auto address : {
             std::uint64_t{0x0000000000000000ULL},
             std::uint64_t{0x0000000000001000ULL},
             std::uint64_t{0x0000123456789000ULL},
             std::uint64_t{0xfffffffffffffffCULL},
         }) {
        const auto emission =
            lower_one({
                make_type3_header(
                    astraea::graphics::
                        kPm4WriteDataOpcode,
                    3U),
                astraea::graphics::
                    kPm4WriteDataSupportedControlWord,
                static_cast<std::uint32_t>(
                    address & 0xffffffffULL),
                static_cast<std::uint32_t>(
                    address >> 32U),
                0x12345678U,
            });

        REQUIRE(
            require_memory_write(emission).
                destination.value ==
            address);
    }
}

TEST_CASE(
    "WRITE_DATA semantic equality excludes packet provenance",
    "[graphics][pm4][write-data][equality]") {
    constexpr std::uint64_t address =
        0x0000123456789000ULL;

    const auto single =
        astraea::graphics::frame_pm4_type3_stream(
            make_stream({
                make_type3_header(
                    astraea::graphics::
                        kPm4WriteDataOpcode,
                    3U),
                astraea::graphics::
                    kPm4WriteDataSupportedControlWord,
                static_cast<std::uint32_t>(
                    address & 0xffffffffULL),
                static_cast<std::uint32_t>(
                    address >> 32U),
                0xdeadbeefU,
            }));
    REQUIRE(single.has_value());

    const auto prefixed =
        astraea::graphics::frame_pm4_type3_stream(
            make_stream({
                make_type3_header(0x70U, 0U),
                0x11111111U,
                make_type3_header(
                    astraea::graphics::
                        kPm4WriteDataOpcode,
                    3U),
                astraea::graphics::
                    kPm4WriteDataSupportedControlWord,
                static_cast<std::uint32_t>(
                    address & 0xffffffffULL),
                static_cast<std::uint32_t>(
                    address >> 32U),
                0xdeadbeefU,
            }));
    REQUIRE(prefixed.has_value());
    REQUIRE(prefixed->frames.size() == 2U);

    const auto left =
        astraea::graphics::
            lower_pm4_write_data_frame_to_graphics_ir(
                single->frames[0]);
    const auto right =
        astraea::graphics::
            lower_pm4_write_data_frame_to_graphics_ir(
                prefixed->frames[1]);
    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    REQUIRE(
        left->provenance.source_packet !=
        right->provenance.source_packet);
    REQUIRE(
        astraea::graphics::
            graphics_ir_semantically_equal(
                left.value(),
                right.value()));

    auto changed_address = right.value();
    std::get<
        astraea::graphics::GraphicsIrGpuMemoryWrite>(
        changed_address.operation)
        .destination.value += 4U;
    REQUIRE_FALSE(
        astraea::graphics::
            graphics_ir_semantically_equal(
                left.value(),
                changed_address));

    auto changed_value = right.value();
    std::get<
        astraea::graphics::GraphicsIrGpuMemoryWrite>(
        changed_value.operation)
        .values[0] = 0xabcdef01U;
    REQUIRE_FALSE(
        astraea::graphics::
            graphics_ir_semantically_equal(
                left.value(),
                changed_value));
}

TEST_CASE(
    "framed stream preserves WRITE_DATA exact packet word offset",
    "[graphics][pm4][write-data][handoff]") {
    const auto framed =
        astraea::graphics::frame_pm4_type3_stream(
            make_stream({
                make_type3_header(0x70U, 0U),
                0x11111111U,
                make_type3_header(
                    astraea::graphics::
                        kPm4WriteDataOpcode,
                    3U),
                astraea::graphics::
                    kPm4WriteDataSupportedControlWord,
                0x00001000U,
                0U,
                0xdeadbeefU,
            }));
    REQUIRE(framed.has_value());
    REQUIRE(framed->frames.size() == 2U);
    REQUIRE(framed->frames[1].word_offset == 2U);

    const auto lowered =
        astraea::graphics::
            lower_pm4_write_data_frame_to_graphics_ir(
                framed->frames[1]);
    REQUIRE(lowered.has_value());
    REQUIRE(
        lowered->provenance.source_packet.extent.word_offset ==
        2U);
}
