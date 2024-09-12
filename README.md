# VidCap Pacer
**Pacing video frame grabbing to make frame capturing time as ideally as possible.**

## Introuduction
When we grab video frames, frames may not arrive at a constant pace, especially for a low-end camera. For example, if we expect a frame rate of 30 frames per second (30 fps), each frame should ideally arrive at 33.3 millisecond (ms) interval. Unfortunately, if we closely track frame arrival time, we may observe that sometimes two consecutive frames arrive with a very narrow interval (say 20 ms), sometimes, the interval is much longer (say 50 ms). Although the average frame rate may be close to 30 fps, this will cause a serious issue if we are to measure some time-related quantity from video frames (e.g., heart rate and speed). 

We dub the issue "frame stuttering" because we can observe a short burst of frame rate and a long wait for the next frame when the issue is present. If we take a look at a video degraded by this issue, we may spot some short quick movement of an object that is supposed to move smoothly and then observe some slow motion next to that quick movement. However, this issue may not be obvious until you want to measure time-related quantity from the video.

Based on our experiments, some devices (e.g., now discontinued product Intel RealSense camera) were very precise at frame arrival timing. When we asked for 30 fps, frames arrived at intervals very close to 33.3 ms all the time. If we keep calling OpenCV's frame grabbing command cap.grab(), precise timing will be automatically handled for us. Unfortunately, many video capture devices are not precise at frame arrival timing, and we have to take the problem into our own hands.

To respond to this issue, we have created VidCap Pacer, a video grabbing tool with a frame pacer, to grab frames at intervals as ideally as possible. This is important to scientific and engineering research and application where you expect accurate measurement that needs to be done across multiple video frames.

## Challenge and Limitations
Although we pace frame arrival as ideally as possible, there is much challenge that prevent us from eliminating the issue in most devices. This includes
1. **Bandwidth limitation**: If the frame size (width x height) is relatively large, and the frame rate is not very small, each frame will significantly consume the bandwidth, thereby making the device unable to fulfill our request. This is common for a USB 2 web camera. Most devices with a native USB 3 connection handle this issue well.
2. **I/O Activities**: Recording a frame to storage while video capturing is executed may cause delay in for frame grabbing as I/O time is highly varied. Although we use SSD and perform I/O in another thread separate from a frame grabbing thread, we found the I/O thread could still interfere with frame-grabbing timing. If your video is relatively short (e.g. <= 5 minutes), you may set io_buffer_length to cover the entire set of the incoming frames. In this case, VidCap Pacer will not write any frame to storage until all frames are captured and kept in the buffer. This will minimize interference from I/O activities. Fortunately, many recent computers are equipped with a better SSD and more performant CPU. This issue may not be serious unless you want to write high resolution frames.
3. **Thread Scheduling**: VidCap Pacer uses Sleep to put a frame-grabbing thread on hold if it is not a time to grab a frame yet. The thread will continue a few milliseconds before the ideal frame grabbing time. For example, if it will take 10 milliseconds before the ideal time, the thread will sleep for 7 milliseconds and continues 3 milliseconds before the ideal time. We call this 3 milliseconds precapRoughMarginTime (can be specified by users to suit their systems).

Although the thread is supposed to exit its sleep state before the ideal frame grabbing time, thread scheduling and resource competition may prevent it from issuing a grab command in time. Therefore, VidCap Pacer also uses loop spinning to check the time, as well. Basically, if we set precapRoughMarginTime long enough, a thread should resume its execution before the ideal time. However, loop spinning may utilize CPU more than it should be. In addition, thread scheduling and resource competition is somewhat unpredicatable at times and the frame grabbing thread may still be late.

4. **Execution Overhead**: Although the computation overhead from waking up the thread to grabbing a frame may not vary much, a significant delay of frame grabbing is not rare because the time the capturing device spends for frame retrieval may significantly vary for each frame. This results in a thread being so late that even though it does not sleep, it cannot perform frame grabbing in time. We observed this issue mainly with USB 2 cameras, especially cheap ones (say $7 webcams). For these problematic capturing devices, a simple frame retrieval with cap.retrieve(frameMat) occassionally takes very long to finish, even when a frame resolution is not high (e.g., 640 x 480). Such latency is beyond the capability of our frame pacing method. However, this issue was barely observed in (native) USB 3 cameras. Therefore, if the frame timing of your USB 2 camara is unacceptable for your application, you might want to try a camera whose connection is USB 3.

## Installation

## How to Use

## Example Usage

## Used Tools (Dependency)
1. OpenCV
2. JSON for Modern C++
3. {fmt}
4. Boost library
5. OpenMP

## Citation
If you VidCap Pacer in your work, please cite our following work.
```
N. Tangjui and P. Taeprasartsit, "Impacts of Camera Frame Pacing for Video Recording on Time-Related Applications," 
2019 16th International Joint Conference on Computer Science and Software Engineering (JCSSE),
Chonburi, Thailand, 2019, pp. 364-368, doi: 10.1109/JCSSE.2019.8864200.
```
