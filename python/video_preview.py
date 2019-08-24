#!/usr/bin/env python3

import asi
import numpy as np
import matplotlib.pyplot as plt
import cv2

def main():

    info = asi.ASI_CAMERA_INFO()
    asi.ASIGetNumOfConnectedCameras()
    asi.ASIGetCameraProperty(info, 0)
    frame_size = info.MaxWidth * info.MaxHeight
    asi.ASIOpenCamera(info.CameraID)
    asi.ASIInitCamera(info.CameraID)
    asi.ASISetROIFormat(info.CameraID, info.MaxWidth, info.MaxHeight, 1, asi.ASI_IMG_RAW8)
    asi.ASISetControlValue(info.CameraID, asi.ASI_BANDWIDTHOVERLOAD, 94, asi.ASI_FALSE)
    asi.ASISetControlValue(info.CameraID, asi.ASI_HIGH_SPEED_MODE, 1, asi.ASI_FALSE)
    asi.ASISetControlValue(info.CameraID, asi.ASI_EXPOSURE, 10000, asi.ASI_FALSE)
    asi.ASISetControlValue(info.CameraID, asi.ASI_GAIN, 300, asi.ASI_FALSE)
    asi.ASIStartVideoCapture(info.CameraID)

    cv2.namedWindow('video', cv2.WINDOW_NORMAL);
    cv2.resizeWindow('video', 640, 480);

    frame_count = 0
    while True:
        print('frame {:06d}'.format(frame_count))
        (rtn, frame) = asi.ASIGetVideoData(info.CameraID, frame_size, 200)
        frame = np.reshape(frame, (info.MaxHeight, info.MaxWidth))
        frame = cv2.cvtColor(frame, cv2.COLOR_BAYER_BG2BGR)
        cv2.imshow('video', frame)
        cv2.waitKey(1)
        frame_count += 1

if __name__ == "__main__":
    main()

