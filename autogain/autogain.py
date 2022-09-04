#!/usr/bin/env python3

import time
import zwoasi
import numpy as np
import matplotlib.pyplot as plt
import cv2

def main():

    zwoasi.init('../linux_sdk/lib/x64/libASICamera2.so')

    cam = zwoasi.Camera(0)

    # create a bytearray buffer for storing video frame data
    whbi = cam.get_roi_format()
    sz = whbi[0] * whbi[1]
    if whbi[3] == zwoasi.ASI_IMG_RGB24:
        sz *= 3
    elif whbi[3] == zwoasi.ASI_IMG_RAW16:
        sz *= 2
    buffer_ = bytearray(sz)

    # initialize matplotlib window
    fig = plt.figure()
    ax = fig.add_subplot(111)
    plt.ion()

    # set reasonable axis limits
    ax.set_ylim(-80.0, 0.0)
    ax.set_xlim(0.0, 255.0)
    ax.grid(True)

    # img = cam.capture_video_frame(buffer_=buffer_, timeout=None)
    index = 0
    # img = plt.imread('/home/rgottula/Desktop/webcam_dumps/webcam_dump_0028/frame_{:06d}.jpg'.format(index))
    img = cam.capture(initial_sleep=None, buffer_=buffer_)
    img = cv2.cvtColor(img, cv2.COLOR_BayerBG2RGB)

    # cv2.imshow('frame', img)
    # cv2.waitKey(1)

    # implot = ax.imshow(img)
    line = ax.plot(np.arange(256), np.zeros(256))[0]
    plt.pause(0.1)
    index += 1

    frames_per_show = 5

    cam.start_video_capture()

    gain = 510

    start_time = time.time()
    while True:
        img = cam.capture_video_frame(buffer_=buffer_, timeout=None)
        # img = cam.capture(initial_sleep=None, buffer_=buffer_)
        # img = plt.imread('/home/rgottula/Desktop/webcam_dumps/webcam_dump_0028/frame_{:06d}.jpg'.format(index))
        index += 1

        if index % frames_per_show == 0:
            # img = cv2.cvtColor(img, cv2.COLOR_BayerBG2RGB)
            # implot.set_data(img)
            # plt.pause(0.00001)

            # integer version, to make Justin happy
            (hist_int,edges_int) = np.histogram(img, bins=256, range=(0,255), density=False)

            (hist,edges) = np.histogram(img, bins=256, range=(0,255), density=True)
            line.set_ydata(10*np.log10(hist))
            plt.pause(0.00001)
            fig.canvas.draw()
            fig.canvas.flush_events()

            # account for hot pixels by making the bin threshold slightly higher than zero
            if hist_int[255] > 5:
                gain -= 1
            else:
                gain += 1

            if gain > 510:
                gain = 510
            elif gain < 0:
                gain = 0

            cam.set_control_value(zwoasi.ASI_GAIN, gain)


            end_time = time.time()
            print(str(frames_per_show / (end_time - start_time))
                + ' ' + str(cam.get_dropped_frames())
                + ' ' + str(gain)
                + ' ' + str(int(hist_int[255])))
            start_time = end_time


if __name__ == "__main__":
    main()