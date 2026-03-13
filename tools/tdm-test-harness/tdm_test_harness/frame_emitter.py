"""Converts sample data into fake AnalyzerFrame objects for testing the HLA
outside of Logic 2.

The emitted frames replicate the structure that the TdmAnalyzer LLA produces:
one FrameV2 per slot per TDM frame, with correct timing for sample rate
derivation.
"""


class FakeFrame:
    """Minimal AnalyzerFrame stand-in for testing outside Logic 2."""

    def __init__(self, frame_type, start_time, end_time, data):
        self.type = frame_type
        self.start_time = start_time
        self.end_time = end_time
        self.data = data


def emit_frames(sample_frames, sample_rate, slot_list, start_frame_num=0):
    """Generate a sequence of FakeFrame objects from sample data.

    Args:
        sample_frames: List of lists — each inner list has one sample value
                       per channel, in the same order as slot_list.
        sample_rate: Audio sample rate in Hz (used for frame timing).
        slot_list: Ordered list of slot indices (e.g. [0, 1]).
        start_frame_num: Starting frame_number (default 0).

    Yields:
        FakeFrame objects in the order the LLA would emit them: for each
        TDM frame, one slot frame per slot in slot_list.
    """
    for frame_idx, samples in enumerate(sample_frames):
        frame_num = start_frame_num + frame_idx
        t = frame_num / sample_rate
        slot_duration = 1.0 / (sample_rate * len(slot_list))

        for ch_idx, slot in enumerate(slot_list):
            slot_start = t + ch_idx * slot_duration
            yield FakeFrame(
                frame_type='slot',
                start_time=slot_start,
                end_time=slot_start + slot_duration,
                data={
                    'slot': slot,
                    'data': samples[ch_idx] if ch_idx < len(samples) else 0,
                    'frame_number': frame_num,
                    'severity': 'ok',
                    'short_slot': False,
                    'extra_slot': False,
                    'bitclock_error': False,
                    'missed_data': False,
                    'missed_frame_sync': False,
                    'low_sample_rate': False,
                },
            )


if __name__ == '__main__':
    # Self-test
    test_samples = [[100, 200], [300, 400], [500, 600]]
    frames = list(emit_frames(test_samples, 48000, [0, 1]))

    assert len(frames) == 6, f"Expected 6 frames (3 TDM frames * 2 slots), got {len(frames)}"

    # Check first TDM frame
    assert frames[0].data['slot'] == 0
    assert frames[0].data['data'] == 100
    assert frames[0].data['frame_number'] == 0
    assert frames[1].data['slot'] == 1
    assert frames[1].data['data'] == 200
    assert frames[1].data['frame_number'] == 0

    # Check frame numbering with offset
    frames = list(emit_frames(test_samples, 48000, [0, 1], start_frame_num=5))
    assert frames[0].data['frame_number'] == 5
    assert frames[2].data['frame_number'] == 6

    # Timing: slot 0 of frame 1 should be at 1/48000 seconds
    frames = list(emit_frames(test_samples, 48000, [0, 1]))
    assert abs(frames[2].start_time - 1.0 / 48000) < 1e-10

    print("All frame_emitter self-tests passed.")
