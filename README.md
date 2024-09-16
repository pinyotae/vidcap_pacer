# VidCap Pacer
**Pacing video frame grabbing to make frame capturing time as ideally as possible.**

## Introuduction
When we grab video frames, frames may not arrive at a constant pace, especially for a low-end camera. For example, if we expect a frame rate of 30 frames per second (30 fps), each frame should ideally arrive at 33.3 millisecond (ms) interval. Unfortunately, if we closely track frame arrival time, we may observe that sometimes two consecutive frames arrive with a very narrow interval (say 20 ms), sometimes, the interval is much longer (say 50 ms). Although the average frame rate may be close to 30 fps, this will cause a serious issue if we are to measure some time-related quantity from video frames (e.g., heart rate and speed). 

We dub the issue "frame stuttering" because we can observe a short burst of frame rate and a long wait for the next frame when the issue is present. If we take a look at a video degraded by this issue, we may spot some short quick movement of an object that is supposed to move smoothly and then observe some slow motion next to that quick movement. However, this issue may not be obvious until you want to measure time-related quantity from the video.

Based on our experiments, some devices (e.g., now discontinued product Intel RealSense camera) were very precise at frame arrival timing. When we asked for 30 fps, frames arrived at intervals very close to 33.3 ms all the time. If we keep calling OpenCV's frame grabbing command cap.grab(), precise timing will be automatically handled for us. Unfortunately, many video capture devices are not precise at frame arrival timing, and we have to take the problem into our own hands.

To respond to this issue, we have created VidCap Pacer, a video capturing tool with a frame pacer, to grab frames at intervals as ideally as possible. This is important to scientific and engineering research and application where you need accurate measurement across multiple video frames.

## How does it Work?
VidCap Pacer creates two threads: one for frame grabbing and another for I/O. Frames are grabbed and initially stored in a circular buffer by the frame grabbing thread. Then, the I/O thread will read frames in the buffer and write each frame to storage in a PNG format. Both threads use mutex to safely handle the buffer. The frame grabbing thread checks itself against the ideal time before issuing the cap.grab() command. If it is too early the thread will sleep to wait without much CPU utilization. Then, it resumes milliseconds before the ideal frame grabbing time and wait for the ideal time by loop spinning. This part is CPU intensive, but it makes timing much more accurate. If we rely only on thread sleeping, you may find that thread scheduling may not wake the thread up in time.

The image is stored with lossless compression because VidCap Pacer is aimed at creating a high quality scientific dataset where biosignals may be interfered with other signals. (We do not allow other image formats as of now, but it is planned.) Frames are initially stored in separate files, and the user can set the program to combine these files and create a single video when it wraps up the processing. The video file is subject to lossy compression. We, however, can resort to the image files saved in lossless compression for better data quality.

## Challenge and Limitations
Although we pace frame arrival as ideally as possible, there is much challenge that prevent us from eliminating the issue in most devices. This includes
1. **Bandwidth limitation**: If the frame size (width x height) is relatively large, and the frame rate is not very small, each frame will significantly consume the bandwidth, thereby making the device unable to fulfill our request. This is common for a USB 2 web camera. Most devices with a native USB 3 connection handle this issue well.

2. **I/O Activities and Image Encoding**: Recording frames during video capture can introduce delays due to inconsistent I/O times. Even with SSDs and separate I/O threads, performance can be affected, especially on older or less powerful CPUs. For shorter videos (e.g., under 5 minutes), consider setting ```io_buffer_length``` to accommodate all incoming frames. This prevents I/O operations from disrupting frame grabbing because they will not be executed until all frames are captured. Modern hardware (e.g., recent CPUs and SSDs) generally mitigates this issue. However, it's still a consideration when capturing high-resolution frames.

3. **Thread Scheduling**: VidCap Pacer uses Sleep to put a frame-grabbing thread on hold if it is not a time to grab a frame yet. The thread will continue a few milliseconds before the ideal frame grabbing time. For example, if it will take 10 milliseconds before the ideal time, the thread will sleep for 7 milliseconds and continues 3 milliseconds before the ideal time. We call this 3 milliseconds precapRoughMarginTime (can be specified by users to suit their systems).
<br/><br/> Although the thread is supposed to exit its sleep state before the ideal frame grabbing time, thread scheduling and resource competition may prevent it from issuing a grab command in time. Therefore, VidCap Pacer also uses loop spinning to check the time, as well. Basically, if we set precapRoughMarginTime long enough, a thread should resume its execution before the ideal time. However, loop spinning may utilize CPU more than it should be. In addition, thread scheduling and resource competition is somewhat unpredicatable at times and the frame grabbing thread may still be late.

4. **Execution Overhead**: Although the computation overhead from waking up the thread to grabbing a frame may not vary much, a significant delay of frame grabbing is not rare because the time the capturing device spends for frame retrieval may significantly vary for each frame. This results in a thread being so late that even though it does not sleep, it cannot perform frame grabbing in time. We observed this issue mainly with USB 2 cameras, especially cheap ones (say $7 webcams). For these problematic capturing devices, a simple frame retrieval with cap.retrieve(frameMat) occassionally takes very long to finish, even when a frame resolution is not high (e.g., 640 x 480). Such latency is beyond the capability of our frame pacing method. However, this issue was barely observed in USB 3 cameras. Therefore, if the frame timing of your USB 2 camara is unacceptable for your application, you might want to try a camera whose connection is USB 3.

## Installation
We provide a binary for Windows 11 64 bit. Please follow the steps below.
1. Download [pre-built opencv_world libary](https://github.com/pinyotae/video_frame_pacer/releases/tag/OpenCV).
2. Download [VidCap Pacer executable](https://github.com/pinyotae/video_frame_pacer/releases/tag/VidCapPacer). Put it in the same folder as the opencv_world library.
3. Download [a video capture settings JSON file](https://github.com/pinyotae/video_frame_pacer/releases/tag/VidCapPacer) (same link as VidCap Pacer). This is a program parameter template that you will edit before real use.
4. Install [VC++ redistributable Runtime for VC17 (Visual C++ 2022) 64 bits](https://aka.ms/vs/17/release/vc_redist.x64.exe).

## How to Use
1. Edit the settings JSON file. Usually, you will change the following arguments:
   <br>series_name
   <br>output_folder
   <br>target_frame_per_sec
   <br>record_time_sec
   If you have multiple cameras, you may need to set camera_id to the one you want. It is an integer starts from 0. Most likely, your preferred camera will have ID 0, 1, or 2.
   Please read the next section for full detail of VidCap Pacer JSON arguments. You may need to adjust precap_rough_margin_time and precap_fine_margin_time to minimize frame grabbing time error.
2. Open a terminal (cmd.exe) and run command ```VidCapPacer "path to json config file"```. For example, ```VidCapPacer video_capture_settings.json```.
3. Observe the average grabbing time error, referred to as deviation time, at the end of the report. Try adjusting some parameters to minimize the error.

## VidCap Pacer JSON Arguments
1. "series_name" (string): the name of frame series. Output files will be prefixed with series_name.
2. "output_folder" (string): folder to store image frames, video, and reports.
3. "time_stamp_report_file_name" (string): base file name of a time stamp report showing when each frame is grabbed and retrieved. This will help analyze the frame timing.
4. "time_deviation_report_file_name" (string): base file name of a deviation time when compared with ideal frame grabbing time. This tells you how much the frame grabbing time error for each frame is. At the end of the file, it shows the average time error (referred to as time deviation).
5. "series_name_report_prefix" (boolean): if true, the the two report files above will be prefixed by the series name. For example, if the series_name = "demo" and time_stamp_report_file_name = "frame_time_stamp.tab" and series_name_report_prefix = true, the final time stamp report file name will be "demo_frame_time_stamp.tab."
6. "io_buffer_length" (integer): the number of frames in a circular frame buffer. If these buffering frames >= the frames needed for the entire video series, the frame saving thread will not be created. Instead, once all frames are captured to the buffer, the frame saving function will be called to save the frames. This ensures that the I/O thread will not compete with the frame grabbing thread for any resource.
	
7. "camera_id" (non-negative integer): the camera ID regarding to the OpenCV library.
8. "frame_height" (positive integer): frame height (pixels).
9. "frame_width" (positive integer): frame width (pixels).
10. "target_frame_per_sec" (positive real number): the number of frames per seconds that you expect. The program will send this frame rate to the capturing device. In some cases, the device may explicitly try to use another frame rate. In this case, VidCap Pacer will show a warning message and you can abort or continue the operation.
11. "record_time_sec" (positive integer): the length of video recording (seconds),
12. "precap_rough_margin_time" (positive real number): The time a frame grabbing thread will awake before the ideal frame grabbing time in second unit. Normally, if the frame rate is not too high, a frame grabbing thread will be ready for issuing a frame grabbing command long before the ideal frame grabbing time (say 20 millisecond). Therefore, the thread sleeps to avoid unnecessary CPU utilization. The thread tries to exit the sleep state before the ideal time to avoid delay caused by thread scheduling. For example, if you set this value to 0.015 and the thread arrives a check point just before the frame grabbing command 20 milliseconds early, the thread will sleep until 15 milliseconds before the ideal time. Then, VidCap Pacer will use a loop spinning to wait for an ideal time.
13. "precap_fine_margin_time" (non-negative real number): The time a frame grabbing thread will leave a spinning waiting loop before the ideal time. For example, if this time is set to 0.00005, VidCap Pacer will exit the loop 0.05 millisecond before the ideal time. This time should be calibrate to suit the machine used for video capture. If your CPU is fast, the margin time should be small. If your CPU is slow, the margin time should not be too small.
14. "video_export" (boolean): If true, once all frames are separately saved as image files, they will be read to create a single video file. The image files are preserved. This process may take a long while to finish if the recording time is long.

## Example Usage in Our Research
We applied an earlier version of VidCap Pacer in our research on measuring the heart rate from a non-facial skin. We found that without a precise frame pacing during video capture, the method could not produce an accurate output, especially for a common light source such as a ceiling fluoresence tube and LED downlight. The challenge of heart-rate measuring on a non-facial skin is mainly based on a weaker vital sign when compare a facial skin. Therefore, minimizing errors in every step is essential to obtain an acceptable outcome. If you are interested in this application, please check our paper for more detail [(Link to IEEEXplore)](https://ieeexplore.ieee.org/document/10440333).
```
N. Tangjui and P. Taeprasartsit, 
"Robust Method for Non-Contact Vital Sign Measurement in Videos Acquired in Real-World Light Settings From Skin Less Affected by Blood Perfusion," 
in IEEE Access, vol. 12, pp. 28582-28597, 2024, doi: 10.1109/ACCESS.2024.3367775.
```

## Used Tools (Dependency)
1. [OpenCV 4.10](https://opencv.org/)
2. [JSON for Modern C++](https://github.com/nlohmann/json)
3. [{fmt} 10.2.0](https://github.com/fmtlib/fmt)
4. [Boost library 1.85.0](https://www.boost.org/)
5. [OpenMP](https://www.openmp.org/). It is bundled in most popular C/C++ compiler suites. You don't need to download it, but you need to specify a correct compiler flag.

## FAQ
1. **Q**: Why do you use a JSON file to set program arguments, instead of command line arguments?
   <br>**A**: There are many arguments that you may need to set, the command line will be so long that it is hard to read. On top of that, a JSON file provides a self documentation, making your video capturing work systematically reproducible. We find that it is helpful to consistently create a dataset.
   
2. **Q**: Based on your source code, it seems that threading and synchonization are done with C++ standard library (e.g., ```<thread>```). Why do you need OpenMP here?
<br>**A**: Earlier we implemented parallelism in this program with OpenMP, but later on, we switched to the standard library as recent compilers provide better support. However, we still rely on OpenMP for high resolution wall clock time (omp_get_wtime). We know that chrono can do this job, but chrono functions are changing in recent C++ compilers, especially the use of its count function. So, we stick with OpenMP for high resolution timing for now until C++ 20 gets better support across major compiler vendors. On a side note, the first public release of VidCap Pacer was compiled with C++17.

3. **Q**: How can we minimize the frame capture time deviation/error?
<br>**A**: There is more than one way to do it. First, you might want to modify precap_rough_margin_time and precap_fine_margin_time parameters and check if you get acceptable time deviation. You might also want to run VidCap Pacer in a system with 4 or more physical cores so that competition for CPU among threads will be less problematic. The next thing that may be helpful is testing frame recording with 30 to 60 seconds with the buffer length adequate to hold the entire video sequence. For example, for 60 seconds recording with 30 fps, you may set io_buffer_length to 1,800 or higher. This will prevent VidCap Pacer from creating a separate I/O thread. It will store everything in the buffer and save frames only when frame capturing is fully finished. Lastly, and perhaps very importantly, change your camera to a better one. A USB 3 camera usually performs much better in frame timing. It may be more expensive, but it worths buying if you want to create a reliable scientific dataset.

## Citation
If you VidCap Pacer in your work, please cite our following work.
```
N. Tangjui and P. Taeprasartsit, "Impacts of Camera Frame Pacing for Video Recording on Time-Related Applications," 
2019 16th International Joint Conference on Computer Science and Software Engineering (JCSSE),
Chonburi, Thailand, 2019, pp. 364-368, doi: 10.1109/JCSSE.2019.8864200.
```
