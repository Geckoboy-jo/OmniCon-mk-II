import ctypes
import unittest
from types import SimpleNamespace
from unittest import mock

import xinput_server


class TimerResolutionTests(unittest.TestCase):
    def test_timer_is_requested_during_polling_and_released_on_error(self):
        winmm = mock.Mock()
        winmm.timeBeginPeriod.return_value = 0
        winmm.timeEndPeriod.return_value = 0

        def run_bridge(xi):
            winmm.timeBeginPeriod.assert_called_once_with(1)
            winmm.timeEndPeriod.assert_not_called()
            raise RuntimeError("poll failed")

        with mock.patch.object(ctypes, "windll", SimpleNamespace(winmm=winmm), create=True), \
             mock.patch.object(xinput_server, "_load_xinput", return_value=None), \
             mock.patch.object(xinput_server, "run_bridge", side_effect=run_bridge):
            with self.assertRaisesRegex(RuntimeError, "poll failed"):
                xinput_server.main()

        winmm.timeEndPeriod.assert_called_once_with(1)

    def test_failed_timer_request_is_reported_and_not_released(self):
        winmm = mock.Mock()
        winmm.timeBeginPeriod.return_value = 97
        stderr = mock.Mock()

        with mock.patch.object(ctypes, "windll", SimpleNamespace(winmm=winmm), create=True), \
             mock.patch.object(xinput_server, "_load_xinput", return_value=None), \
             mock.patch.object(xinput_server, "run_bridge") as run_bridge, \
             mock.patch.object(xinput_server.sys, "stderr", stderr):
            xinput_server.main()

        run_bridge.assert_called_once_with(None)
        winmm.timeEndPeriod.assert_not_called()
        self.assertIn("Could not request 1 ms timer resolution: 97",
                      "".join(call.args[0] for call in stderr.write.call_args_list))


if __name__ == "__main__":
    unittest.main()
