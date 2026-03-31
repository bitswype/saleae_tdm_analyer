// TDM Analyzer correctness test runner.
//
// Runs all test categories and reports pass/fail summary.
// Individual tests are defined in separate files grouped by category.
// See tests/TESTING.md for documentation of the test architecture.
//
// Usage:
//   tdm_correctness
//
// Exit code 0 = all pass, 1 = any failure.

#include "tdm_test_helpers.h"

// ---------------------------------------------------------------------------
// Test framework globals
// ---------------------------------------------------------------------------

int g_pass = 0;
int g_fail = 0;
bool g_test_failed = false;

// ---------------------------------------------------------------------------
// Forward declarations: test_decode_values.cpp
// ---------------------------------------------------------------------------

void test_stereo_16bit_baseline();
void test_lsb_first();
void test_right_aligned_24in32();
void test_left_aligned_24in32();
void test_8channel_slot_numbering();
void test_dsp_mode_b();
void test_frame_sync_inverted();
void test_neg_edge_sampling();
void test_32bit_data();
void test_64bit_data();
void test_mono_single_slot();

void test_combo_8ch_24in32_right_lsb();
void test_combo_modeb_fsinv_negedge();
void test_combo_32ch_32bit_lsb_modeb();
void test_combo_mono_24in32_right_fsinv_96k();
void test_combo_all_nondefault();
void test_combo_4ch_24in32_advanced();

void test_counter_wrap_8bit();
void test_3bit_slots();
void test_8bit_slots();
void test_lsb_right_aligned_8in16();
void test_lsb_left_aligned_8in16();
void test_extreme_padding_right_2in64();
void test_extreme_padding_left_2in64();
void test_63in64_right();
void test_256_slots();
void test_right_aligned_zero_padding();

// ---------------------------------------------------------------------------
// Forward declarations: test_sign_conversion.cpp
// ---------------------------------------------------------------------------

void test_sign_16bit_positive();
void test_sign_16bit_negative();
void test_sign_16bit_zero();
void test_sign_24bit_negative();
void test_sign_32bit_min();
void test_sign_edge_cases();
void test_signed_4bit_end_to_end();
void test_sign_64bit_after_fix();
void test_framev2_signed_decode();

// ---------------------------------------------------------------------------
// Forward declarations: test_error_conditions.cpp
// ---------------------------------------------------------------------------

void test_short_slot_detection();
void test_extra_slot_detection();
void test_clean_signal_no_errors();
void test_misconfig_fewer_slots_than_expected();
void test_misconfig_more_slots_than_expected();
void test_misconfig_wrong_bit_depth();
void test_misconfig_wrong_dsp_mode();
void test_misconfig_wrong_fs_polarity();
void test_minimum_config();

// ---------------------------------------------------------------------------
// Forward declarations: test_advanced_analysis.cpp
// ---------------------------------------------------------------------------

void test_bitclock_error_detection();
void test_missed_data_detection();
void test_missed_frame_sync_detection();

// ---------------------------------------------------------------------------
// Forward declarations: test_generator_blindspots.cpp
// ---------------------------------------------------------------------------

void test_padding_bits_high();
void test_dsp_mode_a_offset_bit_high();
void test_low_sample_rate();

// ---------------------------------------------------------------------------
// Forward declarations: test_framev2.cpp
// ---------------------------------------------------------------------------

void test_framev2_happy_path();
void test_framev2_short_slot_severity();
void test_framev2_extra_slot_severity();
void test_framev2_bitclock_error_severity();
void test_framev2_missed_data_severity();
void test_framev2_missed_frame_sync_severity();
void test_framev2_low_sample_rate();

// ---------------------------------------------------------------------------
// Forward declarations: test_audio_batch.cpp
// ---------------------------------------------------------------------------

void test_batch_off_identical();
void test_batch_basic();
void test_batch_pcm_correctness();
void test_batch_frame_number();
void test_batch_sample_rate();
void test_batch_v1_still_emitted();
void test_batch_32bit();
void test_batch_signed();
void test_batch_1_frame();

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    std::cout << "TDM Analyzer Correctness Tests" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << std::endl;

    std::cout << "Happy Path -- Value Correctness:" << std::endl;
    RunTest( "test_stereo_16bit_baseline", test_stereo_16bit_baseline );
    RunTest( "test_lsb_first", test_lsb_first );
    RunTest( "test_right_aligned_24in32", test_right_aligned_24in32 );
    RunTest( "test_left_aligned_24in32", test_left_aligned_24in32 );
    RunTest( "test_8channel_slot_numbering", test_8channel_slot_numbering );
    RunTest( "test_dsp_mode_b", test_dsp_mode_b );
    RunTest( "test_frame_sync_inverted", test_frame_sync_inverted );
    RunTest( "test_neg_edge_sampling", test_neg_edge_sampling );
    RunTest( "test_32bit_data", test_32bit_data );
    RunTest( "test_64bit_data", test_64bit_data );
    RunTest( "test_mono_single_slot", test_mono_single_slot );
    std::cout << std::endl;

    std::cout << "Sign Conversion:" << std::endl;
    RunTest( "test_sign_16bit_positive", test_sign_16bit_positive );
    RunTest( "test_sign_16bit_negative", test_sign_16bit_negative );
    RunTest( "test_sign_16bit_zero", test_sign_16bit_zero );
    RunTest( "test_sign_24bit_negative", test_sign_24bit_negative );
    RunTest( "test_sign_32bit_min", test_sign_32bit_min );
    RunTest( "test_sign_edge_cases", test_sign_edge_cases );
    RunTest( "test_signed_4bit_end_to_end", test_signed_4bit_end_to_end );
    RunTest( "test_sign_64bit_after_fix", test_sign_64bit_after_fix );
    std::cout << std::endl;

    std::cout << "Error Conditions:" << std::endl;
    RunTest( "test_short_slot_detection", test_short_slot_detection );
    RunTest( "test_extra_slot_detection", test_extra_slot_detection );
    RunTest( "test_clean_signal_no_errors", test_clean_signal_no_errors );
    std::cout << std::endl;

    std::cout << "Combination Tests:" << std::endl;
    RunTest( "test_combo_8ch_24in32_right_lsb", test_combo_8ch_24in32_right_lsb );
    RunTest( "test_combo_modeb_fsinv_negedge", test_combo_modeb_fsinv_negedge );
    RunTest( "test_combo_32ch_32bit_lsb_modeb", test_combo_32ch_32bit_lsb_modeb );
    RunTest( "test_combo_mono_24in32_right_fsinv_96k", test_combo_mono_24in32_right_fsinv_96k );
    RunTest( "test_combo_all_nondefault", test_combo_all_nondefault );
    RunTest( "test_combo_4ch_24in32_advanced", test_combo_4ch_24in32_advanced );
    std::cout << std::endl;

    std::cout << "Robustness -- Misconfig / Edge Cases:" << std::endl;
    RunTest( "test_misconfig_fewer_slots_than_expected", test_misconfig_fewer_slots_than_expected );
    RunTest( "test_misconfig_more_slots_than_expected", test_misconfig_more_slots_than_expected );
    RunTest( "test_misconfig_wrong_bit_depth", test_misconfig_wrong_bit_depth );
    RunTest( "test_misconfig_wrong_dsp_mode", test_misconfig_wrong_dsp_mode );
    RunTest( "test_misconfig_wrong_fs_polarity", test_misconfig_wrong_fs_polarity );
    RunTest( "test_minimum_config", test_minimum_config );
    std::cout << std::endl;

    std::cout << "Bit Pattern Coverage:" << std::endl;
    RunTest( "test_counter_wrap_8bit", test_counter_wrap_8bit );
    std::cout << std::endl;

    std::cout << "Boundary Values:" << std::endl;
    RunTest( "test_3bit_slots", test_3bit_slots );
    RunTest( "test_8bit_slots", test_8bit_slots );
    RunTest( "test_lsb_right_aligned_8in16", test_lsb_right_aligned_8in16 );
    RunTest( "test_lsb_left_aligned_8in16", test_lsb_left_aligned_8in16 );
    RunTest( "test_extreme_padding_right_2in64", test_extreme_padding_right_2in64 );
    RunTest( "test_extreme_padding_left_2in64", test_extreme_padding_left_2in64 );
    RunTest( "test_63in64_right", test_63in64_right );
    RunTest( "test_256_slots", test_256_slots );
    RunTest( "test_right_aligned_zero_padding", test_right_aligned_zero_padding );
    std::cout << std::endl;

    std::cout << "Advanced Analysis Error Detection:" << std::endl;
    RunTest( "test_bitclock_error_detection", test_bitclock_error_detection );
    RunTest( "test_missed_data_detection", test_missed_data_detection );
    RunTest( "test_missed_frame_sync_detection", test_missed_frame_sync_detection );
    std::cout << std::endl;

    std::cout << "Generator Blind Spots:" << std::endl;
    RunTest( "test_padding_bits_high", test_padding_bits_high );
    RunTest( "test_dsp_mode_a_offset_bit_high", test_dsp_mode_a_offset_bit_high );
    RunTest( "test_low_sample_rate", test_low_sample_rate );
    std::cout << std::endl;

    std::cout << "FrameV2 Field Verification:" << std::endl;
    RunTest( "test_framev2_happy_path", test_framev2_happy_path );
    RunTest( "test_framev2_signed_decode", test_framev2_signed_decode );
    RunTest( "test_framev2_short_slot_severity", test_framev2_short_slot_severity );
    RunTest( "test_framev2_extra_slot_severity", test_framev2_extra_slot_severity );
    RunTest( "test_framev2_bitclock_error_severity", test_framev2_bitclock_error_severity );
    RunTest( "test_framev2_missed_data_severity", test_framev2_missed_data_severity );
    RunTest( "test_framev2_missed_frame_sync_severity", test_framev2_missed_frame_sync_severity );
    RunTest( "test_framev2_low_sample_rate", test_framev2_low_sample_rate );
    std::cout << std::endl;

    std::cout << "Audio Batch Mode:" << std::endl;
    RunTest( "test_batch_off_identical", test_batch_off_identical );
    RunTest( "test_batch_basic", test_batch_basic );
    RunTest( "test_batch_pcm_correctness", test_batch_pcm_correctness );
    RunTest( "test_batch_frame_number", test_batch_frame_number );
    RunTest( "test_batch_sample_rate", test_batch_sample_rate );
    RunTest( "test_batch_v1_still_emitted", test_batch_v1_still_emitted );
    RunTest( "test_batch_32bit", test_batch_32bit );
    RunTest( "test_batch_signed", test_batch_signed );
    RunTest( "test_batch_1_frame", test_batch_1_frame );
    std::cout << std::endl;

    std::cout << "==============================" << std::endl;
    std::cout << "Results: " << g_pass << " passed, " << g_fail << " failed, "
              << ( g_pass + g_fail ) << " total" << std::endl;

    return ( g_fail > 0 ) ? 1 : 0;
}
