from saleae.analyzers import HighLevelAnalyzer, AnalyzerFrame, StringSetting, ChoicesSetting


class TdmWavExport(HighLevelAnalyzer):
    """Logic 2 High Level Analyzer that exports selected TDM slots to a WAV file.

    Reads decoded frames from the TdmAnalyzer LLA and writes the selected slot
    samples to a standard WAV file using the Python stdlib `wave` module.

    Settings are injected by Logic 2 before __init__ is called. Phase 9 adds
    slot parsing, WAV file open, and state initialization. Phase 10 adds error
    handling, logging, and documentation.
    """

    # -------------------------------------------------------------------------
    # Settings — declared at class level so Logic 2 discovers and renders them
    # in the HLA settings panel before __init__ runs. Logic 2 injects values
    # as instance attributes; do NOT access self.<setting> in the class body.
    # -------------------------------------------------------------------------

    slots = StringSetting(label='Slots (e.g. 0,2,4 or 0-3)')
    output_path = StringSetting(label='Output Path (absolute path to .wav file)')
    bit_depth = ChoicesSetting(['16', '32'], label='Bit Depth')
    bit_depth.default = '16'  # Must be a separate statement — no default= kwarg allowed

    # -------------------------------------------------------------------------
    # result_types — required by Logic 2 for frame label formatting in the UI.
    # Double-brace syntax: {{data.field}} references frame.data['field'].
    # -------------------------------------------------------------------------

    result_types = {
        'status': {'format': '{{data.message}}'},
        'error':  {'format': 'Error: {{data.message}}'}
    }

    def __init__(self):
        # Capture injected setting values as private instance attributes.
        # Logic 2 has already set self.slots, self.output_path, self.bit_depth
        # as strings by the time __init__ is called.
        self._slots_raw = self.slots          # str — e.g. "0,2,4" or "0-3"
        self._output_path = self.output_path  # str — absolute path to .wav file
        # Convert bit depth to int; fallback to 16 handles the edge case where
        # the default attribute was not applied by an older Logic 2 build.
        self._bit_depth = int(self.bit_depth or '16')
        # Phase 9 adds: slot parsing, WAV file open, state initialization

    def decode(self, frame: AnalyzerFrame):
        """Process one FrameV2 from the upstream TdmAnalyzer LLA.

        Advisory frames (frame.type == 'advisory') carry diagnostic messages
        and do not contain sample data — skip them to avoid KeyError on
        frame.data['slot']. All other frames are 'slot' frames.

        Returns None for all frames in Phase 8 (no-op scaffold).
        Phase 9 adds: slot filtering, sample writing, periodic header refresh.
        """
        if frame.type != 'slot':
            return None

        return None
