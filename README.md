# VCap Pacer
**Pacing video frame grabbing to make frame arrival time as ideally as possible.**

## Introuduction
When we grab video frames, frames may not arrive at a constant pace, especially for a low-end camera. For example, if we expect a frame rate of 30 frames per second (30 fps), each frame should ideally arrive at 33.3 millisecond (ms) interval. Unfortunately, if we closely track frame arrival time, we may observe that sometimes two consecutive frames arrive with a very narrow interval (say 20 ms), sometimes, the interval is much longer (say 50 ms). Although the average frame rate may be close to 30 fps, this will cause a serious issue if we are to measure some time-related quantity from video frames (e.g., heart rate and speed). 

We dub the issue "frame stuttering" because we can observe a short burst of frame rate and a long wait for the next frame when the issue is present. If we take a look at a video degraded by this issue, we may spot some short quick movement of an object that is supposed to move smoothly and then observe some slow motion next to that quick movement. However, this issue may not be obvious until you want to measure time-related quantity from the video.

Based on our experiments, some devices (e.g., now discontinued product Intel RealSense camera) were very precise at frame arrival timing. When we asked for 30 fps, frames arrived at intervals very close to 33.3 ms all the time. If we keep calling OpenCV's frame grabbing command cap.grab(), precise timing will be automatically handled for us. Unfortunately, many video capture devices are not

A video grabber tool with a frame pacer to grab frames at an interval as constantly as possible. This is important for a valid scientific and engineering research.
