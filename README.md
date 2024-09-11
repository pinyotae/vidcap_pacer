# VCap Pacer
Pacing video frame grabbing to make frame arrival time as ideally as possible.

## Introuduction
When we grab video frames, frames may not arrive at a constant pace, especially for a low-end camera. For example, if we expect a frame rate of 30 frames per second (30 fps), each frame should ideally arrive at 33.3 millisecond (ms) interval. Unfortunately, if we closely track frame arrival time, we may observe that sometimes two consecutive frames arrive with a very narrow interval (say 20 ms), sometimes, the interval is much longer (say 50 ms). Although the average frame rate may be close to 30 fps, this will cause a serious issue if you have to 

A video grabber tool with a frame pacer to grab frames at an interval as constantly as possible. This is important for a valid scientific and engineering research.
