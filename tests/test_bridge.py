import unittest

from bridge import Frames


class FrameTests(unittest.TestCase):
    def test_fragmented_frames_recover_after_noise_and_overflow(self):
        frames = Frames()
        self.assertEqual(list(frames.feed(b"STA")), [])
        self.assertEqual(list(frames.feed(b"TE:90,0,0,0\r\nOK\n")), [b"STATE:90,0,0,0", b"OK"])
        self.assertEqual(list(frames.feed(b"\xffBAD\n" + b"x" * 300 + b"\nSTATUS?\n")), [b"STATUS?"])
