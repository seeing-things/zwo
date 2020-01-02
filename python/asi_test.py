"""Integration tests for the asi package.

The asi package is a SWIG-generated Python interface to the C API provided by ZWO.
"""

import time
import unittest
import random
import numpy as np
import imageio

# pylint: disable=import-error
import asi

class TestASI(unittest.TestCase):
    """Collection of integration tests for asi package."""

    def setUp(self):
        """Handle standard camera initialization before test execution."""
        self.num_cameras = asi.ASIGetNumOfConnectedCameras()
        self.assertGreaterEqual(self.num_cameras, 1, 'need at least one connected camera to test')
        rtn, self.info = asi.ASIGetCameraProperty(0)
        self.assertEqual(rtn, asi.ASI_SUCCESS)
        self.assertGreater(len(self.info.Name), 0)
        self.assertEqual(asi.ASIOpenCamera(self.info.CameraID), asi.ASI_SUCCESS)
        self.assertEqual(asi.ASIInitCamera(self.info.CameraID), asi.ASI_SUCCESS)

    def tearDown(self):
        """Close camera after test completes."""
        self.assertEqual(asi.ASICloseCamera(self.info.CameraID), asi.ASI_SUCCESS)

    def set_standard_config(self):
        """Applies a standard configuration to the camera."""

        self.assertEqual(
            asi.ASISetROIFormat(
                self.info.CameraID,
                self.info.MaxWidth,
                self.info.MaxHeight,
                1,
                asi.ASI_IMG_RAW8
            ),
            asi.ASI_SUCCESS
        )

        self.assertEqual(
            asi.ASISetControlValue(
                self.info.CameraID,
                asi.ASI_BANDWIDTHOVERLOAD,
                94,
                asi.ASI_FALSE
            ),
            asi.ASI_SUCCESS
        )

        self.assertEqual(
            asi.ASISetControlValue(self.info.CameraID, asi.ASI_HIGH_SPEED_MODE, 1, asi.ASI_FALSE),
            asi.ASI_SUCCESS
        )

        self.assertEqual(
            asi.ASISetControlValue(self.info.CameraID, asi.ASI_EXPOSURE, 10000, asi.ASI_FALSE),
            asi.ASI_SUCCESS
        )

        self.assertEqual(
            asi.ASISetControlValue(self.info.CameraID, asi.ASI_GAIN, 300, asi.ASI_FALSE),
            asi.ASI_SUCCESS
        )

    # pylint: disable=too-many-locals
    def test_roi(self):
        """Test the ROI API calls."""

        supported_binnings = asi.ASIGetSupportedBins(self.info)

        for _ in range(10):

            binning = random.choice(supported_binnings)
            width = random.randint(1, self.info.MaxWidth // binning // 8) * 8
            height = random.randint(1, self.info.MaxHeight // binning // 2) * 2
            image_type = random.randint(0, 3)

            self.assertEqual(
                asi.ASISetROIFormat(self.info.CameraID, width, height, binning, image_type),
                asi.ASI_SUCCESS
            )

            rtn, width_got, height_got, binning_got, img_type_got = asi.ASIGetROIFormat(
                self.info.CameraID
            )
            self.assertEqual(rtn, asi.ASI_SUCCESS)
            self.assertEqual(width_got, width)
            self.assertEqual(height_got, height)
            self.assertEqual(binning_got, binning)
            self.assertEqual(img_type_got, image_type)


            for _ in range(10):
                x = random.randint(0, (self.info.MaxWidth / binning - width) // 4) * 4
                y = random.randint(0, (self.info.MaxHeight / binning - height) // 2) * 2

                self.assertEqual(asi.ASISetStartPos(self.info.CameraID, x, y), asi.ASI_SUCCESS)
                rtn, x_got, y_got = asi.ASIGetStartPos(self.info.CameraID)
                self.assertEqual(rtn, asi.ASI_SUCCESS)
                self.assertEqual(x_got, x)
                self.assertEqual(y_got, y)


    def test_configs(self):
        """Tests all of the camera configuration API calls."""

        rtn, info = asi.ASIGetCameraProperty(0)
        self.assertEqual(rtn, asi.ASI_SUCCESS)
        self.assertIsInstance(info, asi.ASI_CAMERA_INFO)
        self.assertGreater(len(info.Name), 0)
        del info

        rtn, info = asi.ASIGetCameraPropertyByID(self.info.CameraID)
        self.assertEqual(rtn, asi.ASI_SUCCESS)
        self.assertIsInstance(info, asi.ASI_CAMERA_INFO)
        self.assertGreater(len(info.Name), 0)
        del info

        product_ids = asi.ASIGetProductIDs()
        self.assertIsInstance(product_ids, list)
        self.assertGreater(len(product_ids), 0)

        rtn, num_controls = asi.ASIGetNumOfControls(self.info.CameraID)
        self.assertEqual(rtn, asi.ASI_SUCCESS)
        self.assertGreater(num_controls, 0)

        for control_index in range(num_controls):

            rtn, caps = asi.ASIGetControlCaps(self.info.CameraID, control_index)
            self.assertEqual(rtn, asi.ASI_SUCCESS)
            self.assertIsInstance(caps, asi.ASI_CONTROL_CAPS)
            self.assertGreater(len(caps.Name), 0)

            rtn, value, _ = asi.ASIGetControlValue(self.info.CameraID, caps.ControlType)
            self.assertEqual(rtn, asi.ASI_SUCCESS)
            self.assertIsInstance(value, int)

            if caps.IsWritable:
                self.assertEqual(
                    asi.ASISetControlValue(
                        self.info.CameraID,
                        caps.ControlType,
                        caps.MaxValue,
                        asi.ASI_FALSE
                    ),
                    asi.ASI_SUCCESS
                )

                rtn, value, _ = asi.ASIGetControlValue(self.info.CameraID, caps.ControlType)
                self.assertEqual(rtn, asi.ASI_SUCCESS)
                self.assertEqual(value, caps.MaxValue)

        # Generate and save a BMP image file for use as a darkframe. Filled with uniform random
        # samples, 8-bit grayscale.
        darkframe = np.random.randint(
            low=0,
            high=256,
            size=(self.info.MaxHeight, self.info.MaxWidth),
            dtype='uint8'
        )
        imageio.imwrite('/tmp/test_darkframe.bmp', darkframe)
        self.assertEqual(asi.ASIEnableDarkSubtract(self.info.CameraID, '/tmp/test_darkframe.bmp'),
                         asi.ASI_SUCCESS)
        self.assertEqual(asi.ASIDisableDarkSubtract(self.info.CameraID), asi.ASI_SUCCESS)


    def test_video_capture(self):
        """Test video capture API."""

        self.set_standard_config()

        self.assertEqual(asi.ASIStartVideoCapture(self.info.CameraID), asi.ASI_SUCCESS)

        frame_size = self.info.MaxWidth * self.info.MaxHeight
        num_timeouts = 0
        for _ in range(100):
            (rtn, frame) = asi.ASIGetVideoData(self.info.CameraID, frame_size, 200)
            self.assertIn(rtn, [asi.ASI_SUCCESS, asi.ASI_ERROR_TIMEOUT])
            self.assertEqual(frame.size, frame_size)
            if rtn == asi.ASI_ERROR_TIMEOUT:
                num_timeouts += 1
            rtn, dropped = asi.ASIGetDroppedFrames(self.info.CameraID)
            self.assertEqual(rtn, asi.ASI_SUCCESS)
            self.assertIsInstance(dropped, int)

        self.assertLess(num_timeouts, 2)
        self.assertLess(dropped, 10)
        self.assertEqual(asi.ASIStopVideoCapture(self.info.CameraID), asi.ASI_SUCCESS)

    def test_pulse_guide(self):
        """Test pulse guide port API"""
        for direction in range(4):
            self.assertEqual(asi.ASIPulseGuideOn(self.info.CameraID, direction), asi.ASI_SUCCESS)
            self.assertEqual(asi.ASIPulseGuideOff(self.info.CameraID, direction), asi.ASI_SUCCESS)

    def test_exposure(self):
        """Test single exposure API"""

        exposure_time = 1.0  # in seconds

        self.set_standard_config()

        self.assertEqual(
            asi.ASISetControlValue(
                self.info.CameraID,
                asi.ASI_EXPOSURE,
                int(exposure_time * 1e6),
                asi.ASI_FALSE
            ),
            asi.ASI_SUCCESS
        )

        self.assertEqual(
            asi.ASISetControlValue(
                self.info.CameraID,
                asi.ASI_GAIN,
                0,
                asi.ASI_FALSE
            ),
            asi.ASI_SUCCESS
        )

        rtn, status = asi.ASIGetExpStatus(self.info.CameraID)
        self.assertEqual(rtn, asi.ASI_SUCCESS)
        self.assertEqual(status, asi.ASI_EXP_IDLE)

        start_time = time.time()
        self.assertEqual(asi.ASIStartExposure(self.info.CameraID, asi.ASI_FALSE), asi.ASI_SUCCESS)

        while True:
            rtn, status = asi.ASIGetExpStatus(self.info.CameraID)
            self.assertEqual(rtn, asi.ASI_SUCCESS)
            self.assertIn(status, [asi.ASI_EXP_WORKING, asi.ASI_EXP_SUCCESS])
            if status == asi.ASI_EXP_SUCCESS:
                break

        elapsed_time = time.time() - start_time
        self.assertAlmostEqual(exposure_time, elapsed_time, delta=0.3)

        frame_size = self.info.MaxWidth * self.info.MaxHeight
        rtn, frame = asi.ASIGetDataAfterExp(self.info.CameraID, frame_size)
        self.assertEqual(rtn, asi.ASI_SUCCESS)
        self.assertEqual(frame.size, frame_size)
