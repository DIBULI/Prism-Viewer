# MID360 / MID360S line retention

New recordings preserve SDK `LidarPoint.line` and `line_valid`. Dataset MID
point records remain 16 bytes: byte 14 is line 0..3, byte 15 is validity (0/1).
Zero/zero represents unavailable metadata from an older producer. Invalid
flags and out-of-range lines are rejected, not silently reconstructed.

Playback and ROS1/ROS2 export keep the recorded identifiers. PointCloud2
exports UINT8 `line` at offset 14 and UINT8 `line_valid` at offset 15 with
the existing 20-byte point stride, UINT32 `offset_time` in nanoseconds at
offset 16, and unchanged 100 ms aggregation/timestamp policy.

The Agent generates MID line from the original UDP point index modulo 4,
as in Livox ROS Driver 2, before merging packets. It is not safe to infer it
later from recording or ROS frame indices. Old data without line metadata
remains explicitly unavailable. XT32 ring and explicit timestamps are unchanged.

Build against matching SDK Runtime ABI 17 headers and libraries (also includes unified exposure).
