#include <astraea/execution/sce_agc_first_raster_submission.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint64_t kGeometryProgram =
    0x0000123456000000ULL;
constexpr std::uint64_t kPixelProgram =
    0x0000234567000000ULL;
constexpr std::uint64_t kColorTargetBase =
    0x0000000000001200ULL;

constexpr std::uint32_t make_type3_header(
    std::uint8_t opcode,
    std::uint16_t encoded_count,
    std::uint8_t low_control_bits = 0U) {
    return
        (static_cast<std::uint32_t>(
             astraea::graphics::kPm4Type3PacketType)
         << 30U) |
        ((static_cast<std::uint32_t>(encoded_count) &
          0x3fffU)
         << 16U) |
        (static_cast<std::uint32_t>(opcode) << 8U) |
        static_cast<std::uint32_t>(
            low_control_bits);
}

constexpr std::uint32_t pgm_lo(
    std::uint64_t address) noexcept {
    return static_cast<std::uint32_t>(
        address >> 8U);
}

constexpr std::uint32_t pgm_hi(
    std::uint64_t address) noexcept {
    return static_cast<std::uint32_t>(
        (address >> 40U) & 0xffU);
}

void append_word(
    std::vector<std::byte>& bytes,
    std::uint32_t word) {
    for (std::size_t index = 0U;
         index < 4U;
         ++index) {
        bytes.push_back(
            std::byte{
                static_cast<unsigned char>(
                    (word >> (index * 8U)) &
                    0xffU)});
    }
}

struct PacketRef {
    std::size_t frame_index = 0;
    std::size_t word_offset = 0;
};

class SubmissionBuilder {
public:
    [[nodiscard]] PacketRef add(
        std::initializer_list<std::uint32_t> packet) {
        const PacketRef result{
            .frame_index = frame_count_,
            .word_offset = words_.size(),
        };
        words_.insert(
            words_.end(),
            packet.begin(),
            packet.end());
        ++frame_count_;
        return result;
    }

    [[nodiscard]] astraea::execution::SceAgcDcbSubmission
    build(std::uint8_t flag = 0U) const {
        astraea::execution::SceAgcDcbSubmission submission{
            .submit_description_address =
                astraea::memory::GuestAddress{
                    0x00100000ULL},
            .command_words_address =
                astraea::memory::GuestAddress{
                    0x00200000ULL},
            .word_count =
                static_cast<std::uint32_t>(
                    words_.size()),
            .flag = flag,
            .raw_submit_description = {},
            .opaque_padding = {},
            .command_buffer_bytes = {},
        };

        submission.command_buffer_bytes.reserve(
            words_.size() * 4U);
        for (const auto word : words_) {
            append_word(
                submission.command_buffer_bytes,
                word);
        }
        return submission;
    }

private:
    std::vector<std::uint32_t> words_;
    std::size_t frame_count_ = 0U;
};

struct FirstProfileFixture {
    astraea::execution::SceAgcDcbSubmission submission;

    PacketRef geometry;
    PacketRef pixel;
    PacketRef target_mask;
    PacketRef base;
    PacketRef info;
    PacketRef base_ext;
    PacketRef attrib2;
    PacketRef attrib3;
    PacketRef primitive;
    PacketRef instances;
    PacketRef draw;
};

[[nodiscard]] FirstProfileFixture
make_first_profile(
    std::uint32_t info_value =
        astraea::graphics::
            kGfx10ColorFormatR8G8B8A8 << 2U,
    bool include_color_target = true,
    bool post_draw_mutations = true) {
    SubmissionBuilder builder;

    const auto geometry =
        builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kGeometryEsProgramLoRegisterOffset,
            pgm_lo(kGeometryProgram),
            pgm_hi(kGeometryProgram),
        });

    const auto pixel =
        builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            pgm_lo(kPixelProgram),
            pgm_hi(kPixelProgram),
        });

    PacketRef target_mask{};
    PacketRef base{};
    PacketRef info{};
    PacketRef base_ext{};
    PacketRef attrib2{};
    PacketRef attrib3{};

    if (include_color_target) {
        target_mask =
            builder.add({
                make_type3_header(
                    astraea::graphics::
                        kPm4SetContextRegOpcode,
                    1U),
                astraea::graphics::
                    kColorTargetMaskContextOffset,
                0x0fU,
            });
        base =
            builder.add({
                make_type3_header(
                    astraea::graphics::
                        kPm4SetContextRegOpcode,
                    1U),
                astraea::graphics::
                    kColorTarget0BaseContextOffset,
                static_cast<std::uint32_t>(
                    kColorTargetBase >> 8U),
            });
        info =
            builder.add({
                make_type3_header(
                    astraea::graphics::
                        kPm4SetContextRegOpcode,
                    1U),
                astraea::graphics::
                    kColorTarget0InfoContextOffset,
                info_value,
            });
        base_ext =
            builder.add({
                make_type3_header(
                    astraea::graphics::
                        kPm4SetContextRegOpcode,
                    1U),
                astraea::graphics::
                    kColorTarget0BaseExtContextOffset,
                static_cast<std::uint32_t>(
                    kColorTargetBase >> 40U),
            });
        attrib2 =
            builder.add({
                make_type3_header(
                    astraea::graphics::
                        kPm4SetContextRegOpcode,
                    1U),
                astraea::graphics::
                    kColorTarget0Attrib2ContextOffset,
                (3U << 14U) | 3U,
            });
        attrib3 =
            builder.add({
                make_type3_header(
                    astraea::graphics::
                        kPm4SetContextRegOpcode,
                    1U),
                astraea::graphics::
                    kColorTarget0Attrib3ContextOffset,
                static_cast<std::uint32_t>(
                    astraea::graphics::
                        kGfx10ResourceType2d)
                    << 24U,
            });
    }

    const auto primitive =
        builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4SetUconfigRegOpcode,
                1U),
            astraea::graphics::
                kFirstRasterPrimitiveTypeUconfigOffset,
            astraea::graphics::
                kFirstRasterPrimitiveTypeRaw,
        });

    const auto instances =
        builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4NumInstancesOpcode,
                0U),
            astraea::graphics::
                kFirstRasterInstanceCount,
        });

    const auto draw =
        builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            astraea::graphics::
                kFirstRasterIndexCount,
            astraea::graphics::
                kFirstRasterAutoIndexInitiatorRaw,
        });

    if (post_draw_mutations) {
        (void)builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                1U),
            astraea::graphics::
                kColorTargetMaskContextOffset,
            0U,
        });
        (void)builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4SetUconfigRegOpcode,
                1U),
            astraea::graphics::
                kFirstRasterPrimitiveTypeUconfigOffset,
            6U,
        });
        (void)builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            pgm_lo(0x0000333333000000ULL),
            pgm_hi(0x0000333333000000ULL),
        });
    }

    return FirstProfileFixture{
        .submission = builder.build(),
        .geometry = geometry,
        .pixel = pixel,
        .target_mask = target_mask,
        .base = base,
        .info = info,
        .base_ext = base_ext,
        .attrib2 = attrib2,
        .attrib3 = attrib3,
        .primitive = primitive,
        .instances = instances,
        .draw = draw,
    };
}

[[nodiscard]] astraea::execution::SceAgcRasterPacketSource
register_source(
    PacketRef packet,
    std::size_t value_index = 0U) {
    return astraea::execution::
        SceAgcRasterPacketSource{
            .frame_index = packet.frame_index,
            .packet_word_offset =
                packet.word_offset,
            .value_word_offset =
                packet.word_offset +
                2U +
                value_index,
        };
}

}  // namespace

TEST_CASE(
    "first raster submission snapshots mixed PM4 state and exact provenance",
    "[execution][v3][raster-submission]") {
    const auto fixture =
        make_first_profile();

    const auto result =
        astraea::execution::
            plan_sce_agc_first_raster_submission(
                fixture.submission);

    REQUIRE(result.has_value());

    REQUIRE(
        result->geometry_program_address.value ==
        kGeometryProgram);
    REQUIRE(
        result->pixel_program_address.value ==
        kPixelProgram);

    REQUIRE(
        result->context_register_state.values[
            astraea::graphics::
                kColorTargetMaskContextOffset] ==
        0x0fU);
    REQUIRE(
        result->user_config_register_state.values[
            astraea::graphics::
                kFirstRasterPrimitiveTypeUconfigOffset] ==
        astraea::graphics::
            kFirstRasterPrimitiveTypeRaw);
    REQUIRE(
        result->shader_register_state.values[
            astraea::graphics::
                kPixelProgramLoRegisterOffset] ==
        pgm_lo(kPixelProgram));

    REQUIRE(
        result->draw ==
        astraea::graphics::FirstRasterDrawPlan{
            .primitive_type_raw = 4U,
            .instance_count = 1U,
            .index_count = 3U,
            .initiator_raw = 2U,
        });

    REQUIRE(
        result->color_target_state.base_address.value ==
        kColorTargetBase);
    REQUIRE(result->color_target_state.width == 4U);
    REQUIRE(result->color_target_state.height == 4U);
    REQUIRE(result->color_target_state.write_mask == 0x0fU);

    REQUIRE(
        result->color_target_image.logical_byte_count ==
        64U);
    REQUIRE(
        result->color_target_image.layout.pitch_pixels ==
        64U);
    REQUIRE(
        result->color_target_image.layout.pitch_bytes ==
        256U);
    REQUIRE(
        result->color_target_image.layout.surface_byte_count ==
        1024U);

    REQUIRE(
        result->geometry_pgm_lo_source ==
        register_source(
            fixture.geometry,
            0U));
    REQUIRE(
        result->geometry_pgm_hi_source ==
        register_source(
            fixture.geometry,
            1U));
    REQUIRE(
        result->pixel_pgm_lo_source ==
        register_source(
            fixture.pixel,
            0U));
    REQUIRE(
        result->pixel_pgm_hi_source ==
        register_source(
            fixture.pixel,
            1U));
    REQUIRE(
        result->primitive_type_source ==
        register_source(
            fixture.primitive));
    REQUIRE(
        result->instances_source ==
        astraea::execution::
            SceAgcRasterPacketSource{
                .frame_index =
                    fixture.instances.frame_index,
                .packet_word_offset =
                    fixture.instances.word_offset,
                .value_word_offset =
                    fixture.instances.word_offset + 1U,
            });
    REQUIRE(
        result->draw_source ==
        astraea::execution::
            SceAgcRasterPacketSource{
                .frame_index =
                    fixture.draw.frame_index,
                .packet_word_offset =
                    fixture.draw.word_offset,
                .value_word_offset =
                    fixture.draw.word_offset + 1U,
            });

    REQUIRE(
        result->color_target_sources.target_mask ==
        register_source(fixture.target_mask));
    REQUIRE(
        result->color_target_sources.base ==
        register_source(fixture.base));
    REQUIRE(
        result->color_target_sources.info ==
        register_source(fixture.info));
    REQUIRE(
        result->color_target_sources.base_ext ==
        register_source(fixture.base_ext));
    REQUIRE(
        result->color_target_sources.attrib2 ==
        register_source(fixture.attrib2));
    REQUIRE(
        result->color_target_sources.attrib3 ==
        register_source(fixture.attrib3));
}

TEST_CASE(
    "first raster submission enforces draw ordering and cardinality",
    "[execution][v3][raster-submission][negative][draw]") {
    using Error =
        astraea::execution::
            SceAgcFirstRasterSubmissionErrorCode;

    SECTION("NUM_INSTANCES must precede draw") {
        SubmissionBuilder builder;
        (void)builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,
        });

        const auto result =
            astraea::execution::
                plan_sce_agc_first_raster_submission(
                    builder.build());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::missing_instance_count_before_draw);
    }

    SECTION("second draw is rejected") {
        SubmissionBuilder builder;
        (void)builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4NumInstancesOpcode,
                0U),
            1U,
        });
        (void)builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,
        });
        (void)builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,
        });

        const auto result =
            astraea::execution::
                plan_sce_agc_first_raster_submission(
                    builder.build());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::multiple_draws);
        REQUIRE(result.error().frame_index == 2U);
    }

    SECTION("draw is required") {
        SubmissionBuilder builder;
        (void)builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4NumInstancesOpcode,
                0U),
            1U,
        });

        const auto result =
            astraea::execution::
                plan_sce_agc_first_raster_submission(
                    builder.build());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::missing_draw);
    }
}

TEST_CASE(
    "first raster submission rejects unknown or controlled Type-3 packets",
    "[execution][v3][raster-submission][negative][pm4]") {
    using Error =
        astraea::execution::
            SceAgcFirstRasterSubmissionErrorCode;

    SECTION("unknown opcode") {
        SubmissionBuilder builder;
        (void)builder.add({
            make_type3_header(0x75U, 1U),
            0U,
            0x12345678U,
        });

        const auto result =
            astraea::execution::
                plan_sce_agc_first_raster_submission(
                    builder.build());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_opcode);
        REQUIRE(result.error().opcode == 0x75U);
    }

    SECTION("nonzero low control bits") {
        SubmissionBuilder builder;
        (void)builder.add({
            make_type3_header(
                astraea::graphics::
                    kPm4SetUconfigRegOpcode,
                1U,
                1U),
            astraea::graphics::
                kFirstRasterPrimitiveTypeUconfigOffset,
            4U,
        });

        const auto result =
            astraea::execution::
                plan_sce_agc_first_raster_submission(
                    builder.build());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::
                unsupported_type3_header_control_bits);
        REQUIRE(
            result.error().
                type3_header_control_bits == 1U);
    }
}

TEST_CASE(
    "first raster submission reports missing ColorTarget semantics precisely",
    "[execution][v3][raster-submission][negative][color-target]") {
    using Error =
        astraea::execution::
            SceAgcFirstRasterSubmissionErrorCode;

    const auto fixture =
        make_first_profile(
            astraea::graphics::
                kGfx10ColorFormatR8G8B8A8 << 2U,
            false,
            false);

    const auto result =
        astraea::execution::
            plan_sce_agc_first_raster_submission(
                fixture.submission);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        Error::color_target_decode_failure);
    REQUIRE(
        result.error().color_target_error.has_value());
    REQUIRE(
        result.error().color_target_error->code ==
        astraea::graphics::
            ColorTarget0ContextErrorCode::
                target_mask_uninitialized);
}

TEST_CASE(
    "first raster submission preserves unsupported ColorTarget layout failure",
    "[execution][v3][raster-submission][negative][layout]") {
    using Error =
        astraea::execution::
            SceAgcFirstRasterSubmissionErrorCode;

    const auto fixture =
        make_first_profile(
            (astraea::graphics::
                 kGfx10ColorFormatR8G8B8A8
             << 2U) |
                (1U << 28U),
            true,
            false);

    const auto result =
        astraea::execution::
            plan_sce_agc_first_raster_submission(
                fixture.submission);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        Error::color_target_image_failure);
    REQUIRE(
        result.error().
            color_target_image_error.has_value());
    REQUIRE(
        result.error().
            color_target_image_error->code ==
        astraea::graphics::
            Gfx10ColorTargetImageErrorCode::
                dcc_not_supported);
}
