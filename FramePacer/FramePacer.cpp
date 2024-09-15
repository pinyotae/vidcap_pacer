/**
  The main implementation of VidCap (frame) Pacer to make frame capturing time as ideally as possible.
  More detail and source code is available at https://github.com/pinyotae/video_frame_pacer/blob/main/README.md

  MIT License
  Copyright (c) 2024 Pinyo Taeprasartsit
 */ 

#include <iostream>
#include <fstream>
#include <opencv2/opencv.hpp>
#include <memory>
#include <omp.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <fmt/core.h>
#include <windows.h>
#include "json.hpp"

#define shrptr_VideoCapture std::shared_ptr<cv::VideoCapture> 

using namespace cv;
using namespace std;
using json = nlohmann::json;

shrptr_VideoCapture initVideoCapture(const int camID, const int frameHeight = 480,
	const int frameWidth = 640, const double fps=30);

void captureToMemorySpace(shrptr_VideoCapture cap, const double timeBetweenFrames,
	const int numFrames, const int framesPerSec);

void checkTargetFpsAgainstActualFps(const double targetFPS, const double actualFPS);

void setImgFileNameFormatString(const int numFrames);

void readJsonVidCaptureSettings(string configPath);

void printCaptureSettings();

/* Program arguments :
  1. "series_name" (string): the name of frame series. Output files will be prefixed with series_name.
  2. "output_folder" (string): folder to store image frames, video, and reports.
  3. "time_stamp_report_file_name" (string): base file name of a time stamp report showing when each frame is 
	grabbed and retrieved. This will help analyze the frame timing.

  4. "time_deviation_report_file_name" (string): base file name of a deviation time when compared with ideal 
	frame grabbing time. This tells you how much the frame grabbing time error for each frame is. At the end of 
	the file, it shows the average time error (referred to as time deviation).

  5. "series_name_report_prefix" (boolean): if true, the the two report files above will be prefixed by the 
	series name. For example, if the series_name = "demo" and time_stamp_report_file_name = "frame_time_stamp.tab" 
	and series_name_report_prefix = true, the final time stamp report file name will be "demo_frame_time_stamp.tab."
  6. "io_buffer_length" (integer): the number of frames in a circular frame buffer. If these buffering frames >= the 
	frames needed for the entire video series, the frame saving thread will not be created. Instead, once all frames 
	are captured to the buffer, the frame saving function will be called to save the frames. This ensures that the 
	I/O thread will not compete with the frame grabbing thread for any resource.

  7. "camera_id" (non-negative integer): the camera ID regarding to the OpenCV library.
  8. "frame_height" (positive integer): frame height (pixels).
  9. "frame_width" (positive integer): frame width (pixels).

  10. "target_frame_per_sec" (positive real number): the number of frames per seconds that you expect. The program 
	will send this frame rate to the capturing device. In some cases, the device may explicitly try to use another 
	frame rate. In this case, VidCap Pacer will show a warning message and you can abort or continue the operation.

  11. "record_time_sec" (positive integer): the length of video recording (seconds).

  12. "precap_rough_margin_time" (positive real number): The time a frame grabbing thread will awake before the ideal 
	frame grabbing time in second unit. Normally, if the frame rate is not too high, a frame grabbing thread will be 
	ready for issuing a frame grabbing command long before the ideal frame grabbing time (say 20 millisecond). 
	Therefore, the thread sleeps to avoid unnecessary CPU utilization. The thread tries to exit the sleep state before 
	the ideal time to avoid delay caused by thread scheduling. For example, if you set this value to 0.015 and the 
	thread arrives a check point just before the frame grabbing command 20 milliseconds early, the thread will sleep 
	until 15 milliseconds before the ideal time. Then, VidCap Pacer will use a loop spinning to wait for an ideal time.

  13. "precap_fine_margin_time" (non-negative real number): The time a frame grabbing thread will leave a spinning 
	waiting loop before the ideal time. For example, if this time is set to 0.00005, VidCap Pacer will exit the loop 
	0.05 millisecond before the ideal time. This time should be calibrate to suit the machine used for video capture. 
	If your CPU is fast, the margin time should be small. If your CPU is slow, the margin time should not be too small.

  14. "video_export" (boolean): If true, once all frames are separately saved as image files, they will be read to 
	create a single video file. The image files are preserved. This process may take a long while to finish if the 
	recording time is long.
*/

string seriesName = "demo_";
string outputFolder = "C:/TestingGround/VideoCapture/Images";
string imgFileFormatStr;
string video_out_folder = "C:/TestingGround/VideoCapture/Videos";
string timeStampReportFileName = "time_stamp_report.tab";
string timeDeviationReportFileName = "time_deviation_report.tab";
bool seriesNameReportPrefix = true;  // Series name will be a prefix to the report file name.
int ioBufferLength = 30;

int camID = 0;
int frameHeight = 480;
int frameWidth = 640;
double targetFPS = 15;
int recordTimeSeconds = 3;

/// Capture thread will awake before the expected capture time by the amount of
///   rough margin time. For example, if precapRoughMarginTime is 0.020, the thread will awake 20 ms
///   before the expected capture time.	
/// Then,the capture thread will be in a tight loop until precapFineMarginTime, which should be <= 0.1 ms.
/// This tight loop will occupy the CPU for a short while, but it can virtually eliminate the imprecise
///   frame grabbing time issue in Windows. If your machine has at sufficient cores, this should not be
///   an issue.
double precapRoughMarginTime = 0.020;
double precapFineMarginTime = 0.00005;

bool videoExport = false;

int bufferEndIndex = 0;
int bufferStartIndex = 0;
int framesNotWritten = 0;
int framesLeftToCapture = 0;
std::mutex writeMutex;
int timeBetweenFramesMSec;
cv::Mat saveBuffer;
int numFrames;


int main(int argc, char* argv[]) {
	if (argc == 1) {
		cout << "Please provide the path to video capture settings." << endl;
		return 0;
	}
	else {
		readJsonVidCaptureSettings(argv[1]);
		if (seriesNameReportPrefix) {
			timeStampReportFileName = seriesName + "_" + timeStampReportFileName;
			timeDeviationReportFileName = seriesName + "_" + timeDeviationReportFileName;
		}
		printCaptureSettings();
	}
    cout << "Initializing Video Capture\n";
	cout << "Target frame rate = " << targetFPS << "\n";
	shrptr_VideoCapture cap = initVideoCapture(camID, frameHeight, frameWidth, targetFPS);
	
	numFrames = (int)(targetFPS * recordTimeSeconds);
	cout << "Number of frames = " << numFrames << "\n";
	framesLeftToCapture = numFrames;
	setImgFileNameFormatString(numFrames);
	cap->set(cv::CAP_PROP_FPS, targetFPS);
	double actualFPS = cap->get(cv::CAP_PROP_FPS);
	
	const double timeBetweenFrames = 1.0 / targetFPS;
	timeBetweenFramesMSec = (int)(timeBetweenFrames * 1000);
	cout << "Time between frames = " << timeBetweenFramesMSec << " msec\n";

	cout << "\nStarting Video Capture" << endl;
	captureToMemorySpace(cap, timeBetweenFrames, numFrames, (int) targetFPS);
}


/// <summary>
/// Check wether the target and actual capturing frame rates are the same.
/// Note: a video capturing device may or may not produce the target frame rate we set, 
///   even though the target frame rate is less than its default frame rate. For example,
///   if we ask for 15 fps when its default frame rate is 30 fps, the device may not
///   capture frames at 15 fps, although its capacity should allow it.
/// If you want to build a scientific dataset, this is an essential step because you may
///   have more stringent requirements. For instance, if the light source is subject to
///   50 Hz alternate electric current (not 60 Hz), you might want to target the frame
///   rate to 25 Hz, not 30 Hz, to avoid flickering. However, your video capturing device
///   may stick to 30 Hz.
/// </summary>
/// <param name="targetFPS">Target frame rate (frames per second) that the user asks for.</param>
/// <param name="actualFPS">Actual frame rate that the capturing device will use.</param>
void checkTargetFpsAgainstActualFps(const double targetFPS, const double actualFPS) {
	cout << "Actual frame rate = " << actualFPS << " fps\n";
	if (abs(targetFPS - actualFPS) > 0.01) {
		cout << "Actual frame rate and target frame rate are different: " <<
			targetFPS << " vs " << actualFPS << "\n";
		cout << "This usually happens when your video capture device does not support the target frame rate.\n";
		cout << "The data you collect may be invalid if you proceed unless your target frame rate is relatively small.\n";
		cout << "Press Y to continue with the target frame rate, but please check whether frame arrival time is correct.\n";
		cout << "Press N to break.\n";
		while (true) {
			string s;
			cin >> s;
			if (s[0] == 'N' || s[0] == 'n') {
				exit(0);
			}
			else if (s[0] == 'Y' || s[0] == 'y') {
				return;
			}
		}
	}
	return;
}


shrptr_VideoCapture initVideoCapture(const int camID, const int frameHeight,
		const int frameWidth, const double fps) {
	shrptr_VideoCapture cap(new VideoCapture(camID));	// open the default camera
	if (!cap->isOpened()) { // check if we succeeded
		printf("Cannot open camera ID %d\n", camID);
		printf("Please check your camera ID and make sure it is connected and turned on.\n");
		return nullptr;
	}

	cap->set(cv::CAP_PROP_FRAME_HEIGHT, frameHeight);
	cap->set(cv::CAP_PROP_FRAME_WIDTH, frameWidth);
	cap->set(cv::CAP_PROP_AUTOFOCUS, false);
	cap->set(cv::CAP_PROP_BUFFERSIZE, 30);
	cap->set(cv::CAP_PROP_FPS, fps);
	cap->set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y', 'U', 'Y', '2'));

	return cap;
}


/// <summary>
/// To pace frame arrival, we compute the time our application should wait before
///   issuing the next frame grab command.
/// The wait time end a little bit earlier by marginTime.
/// The early end of wait time is to compensate the incoming work before the actual
///   video capturing. We should calibrate the marginTime to make frame pacing close
///   to the ideal as much as possible.
/// </summary>
/// <param name="nextFrameID">Frame ID of the frame to be grabbed next.</param>
/// <param name="idealTimeBetweenFrame">Ideal time between two consecutive frames. 
///   The frame ID and this ideal time are used to compute the ideal time for next frame grab.</param>
/// <param name="time0">Reference time at the beginning of the first frame interval.</param>
/// <returns></returns>
int waitForNextGrab(int nextFrameID, double idealTimeBetweenFrames, double time0) {
	double currTime = omp_get_wtime();
	double elapsedTime = currTime - time0;
	double nextTime = nextFrameID * idealTimeBetweenFrames;
  
	// Put thread to sleep for precap rough margin time.
	int waitTime = -1;
	if (elapsedTime < nextTime - precapRoughMarginTime) {  // Need to wait until the next time
		waitTime = (int)((nextTime - elapsedTime - precapRoughMarginTime) * 1000);
		if (waitTime > 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));
	}
	
	// Use loop spinning to check time, continue when it is close to the ideal time for next frame grabbing.
	double nextTimeAbsolute = nextTime + time0;
	while (nextTimeAbsolute - omp_get_wtime() > precapFineMarginTime) {
		continue;
	}
	return waitTime;
}


void readJsonVidCaptureSettings(string jsonSettingsPath) {
	ifstream f(jsonSettingsPath);
	json vcaptureSettings = json::parse(f);
	f.close();

	seriesName = vcaptureSettings["series_name"];
	outputFolder = vcaptureSettings["output_folder"];
	timeStampReportFileName = vcaptureSettings["time_stamp_report_file_name"];
	timeDeviationReportFileName = vcaptureSettings["time_deviation_report_file_name"];
	seriesNameReportPrefix = vcaptureSettings["series_name_report_prefix"];
	ioBufferLength = vcaptureSettings["io_buffer_length"];

	camID = vcaptureSettings["camera_id"];
	frameHeight = vcaptureSettings["frame_height"];
	frameWidth = vcaptureSettings["frame_width"];
	targetFPS = vcaptureSettings["target_frame_per_sec"];
	recordTimeSeconds = vcaptureSettings["record_time_sec"];

	precapRoughMarginTime = vcaptureSettings["precap_rough_margin_time"];
	precapFineMarginTime = vcaptureSettings["precap_fine_margin_time"];
	videoExport = vcaptureSettings["video_export"];
}


/// <summary>
///  Display the settings received through JSON settings. This informs the user of
///    the actual settings the program reads from the file.
/// </summary>
void printCaptureSettings() {
	fmt::print("\n===== Video Capture Settings =====\n");
	fmt::print("Series Name: {}\n", seriesName);
	fmt::print("Output Folder: {}\n", outputFolder);
	fmt::print("Time Stamp Report File Name: {}\n", timeStampReportFileName);
	fmt::print("Time Deviation Report File Name: {}\n", timeDeviationReportFileName);
	fmt::print("Use Series Name as Prefix to Report File Name: {}\n", seriesNameReportPrefix);
	fmt::print("I/O Buffer Length: {} frames\n\n", ioBufferLength);

	fmt::print("Camera ID: {}\n", camID);
	fmt::print("Frame Height: {} pixels\n", frameHeight);
	fmt::print("Frame Width: {} pixels\n", frameWidth);
	fmt::print("Target Frames Per Seconds (FPS): {} fps\n", targetFPS);
	fmt::print("Recording time: {} seconds\n\n", recordTimeSeconds);

	fmt::print("Rough Margin Time before Frame Grabbing: {:.5f} seconds\n", precapRoughMarginTime);
	fmt::print("Fine Margin Time before Frame Grabbing: {:.5f} seconds\n", precapFineMarginTime);
	fmt::print("Export to Video: {}\n", videoExport);
	fmt::print("===== ===== ===== ===== ===== =====\n\n");
}


void prepareBufferFrames(std::vector<cv::Mat>& frames, const int height,
		const int width) {
	saveBuffer = cv::Mat(height, width, CV_8UC3);
	for (int frameID = 0; frameID < ioBufferLength; ++frameID) {
		frames.at(frameID) = cv::Mat(height, width, CV_8UC3);
	}
}


/// Retrieve a frame and store it in a frame array. Then, compute an elapsed time
///   based on the reference time0.
/// <returns>Elapsed time from the beginning of processing (time0).</return>
double pushFrameToMat(shrptr_VideoCapture cap, double time0, vector<Mat>& frames, 
		int frameID) {
	cap->retrieve(frames.at(frameID));

	double currTime = omp_get_wtime();
	double elapsedTime = currTime - time0;	// record how much time passed (milli-second)
	int elapsedSecond = (int)elapsedTime;	// record how much time passed (second, drop fractional value).
	return elapsedTime;
}


/// <summary>
/// Save a frame in the buffer to storage and handle buffer indices. 
/// This is one of the core functions of an I/O thread.
/// </summary>
/// <param name="frameID">ID of a frame to be saved.</param>
/// <param name="frames">Pointer to a frame buffer.</param>
void writeFrameToImageFile(int frameID, vector<Mat>* frames) {
	string imgPath = fmt::format(imgFileFormatStr, outputFolder, seriesName, frameID);

	writeMutex.lock(); {
		saveBuffer = frames->at(bufferStartIndex);
		bufferStartIndex = (bufferStartIndex + 1) % ioBufferLength;
		framesNotWritten -= 1;
	}
	writeMutex.unlock();

	imwrite(imgPath, saveBuffer);
}


/// <summary>
/// Retrieve a frame from a capturing device, place it to the buffer, and handle buffer indices.
/// This is one of the core functions of a frame grabbing thread. Note that the frame is grabbed
///   earlier. This function retrieves the grabbed data from the device to the buffer.
/// </summary>
/// <param name="cap">Shared pointer to the capturing device.</param>
/// <param name="time0">Reference time at the beginning of the first frame interval.</param>
/// <param name="frames">Pointer to a frame buffer.</param>
/// <param name="frameID">ID of a frame to be retrieved.</param>
/// <returns>Elapsed time from the beginning of processing (time0).</returns>
double pushFrameToMatCircularBuffer(shrptr_VideoCapture cap, double time0,
		vector<Mat>& frames, int frameID) {
	writeMutex.lock(); {
		if ((bufferEndIndex + 1) % ioBufferLength == bufferStartIndex) {
			printf("Error: I/O buffer is full.\n");
			exit(0);
		}
		bufferEndIndex = (bufferEndIndex + 1) % ioBufferLength;
		cap->retrieve(frames.at(bufferEndIndex));
		framesNotWritten += 1;
		framesLeftToCapture -= 1;
	}
	writeMutex.unlock();

	double currTime = omp_get_wtime();
	double elapsedTime = currTime - time0;	// record how much time passed (milli-second)
	int elapsedSecond = (int)elapsedTime;	// record how much time passed (second, drop fractional value).
	return elapsedTime;
}


void saveFramesThd(vector<Mat>* frames) {
	int frameID = 0;
	while (framesLeftToCapture > 0 || framesNotWritten > 0) {
		if (framesNotWritten == 0) {  // Wait for a grabber to get another frame.
			std::this_thread::sleep_for(std::chrono::milliseconds(timeBetweenFramesMSec));
			continue;
		}
		writeFrameToImageFile(frameID, frames);
		frameID += 1;
	}
}


/// <summary>
/// Report the time stamp of each frame to a file.
/// </summary>
/// <param name="grabTimeStamps">Reference to a vector storing time stamp for each frame grabbing.</param>
/// <param name="retrieveTime">Pointer to a vector storing frame retrieval time stamps.</param>
void reportTimeStamps(vector<double>& grabTimeStamps, vector<double>& retrieveTimeStamps) {
	string timeStampPath = outputFolder + "/" + timeStampReportFileName;
	cout << "\nSaving the time stamp of each frame to " << timeStampPath << "\n";
	ofstream reportFile(timeStampPath);

	reportFile << "FrameID\tGrabTime(s)\tRetrievalTime(s)\n";
	for (int i = 0; i < grabTimeStamps.size(); ++i) {
		reportFile << fmt::format("{}\t{}\t{}\n", i + 1, grabTimeStamps.at(i), retrieveTimeStamps.at(i));
		if (i % 100 == 0)  // Print a dot for each 100 lines saved
			printf(".");
	}
	reportFile.close();
	cout << "\nSaving time stamps DONE" << endl;
}


/// <summary>
/// We want to make frame IDs with running numbers in format 00123 where leading zeros
///   are not too many for the number of frame we aim at. Therefore, this function takes
///   the number of frames we expect and prepare the number of digits including leading
///   zeros accordingly.
/// </summary>
/// <param name="numFrames">The number of frames we expect for video recording.</param>
void setImgFileNameFormatString(const int numFrames) {
	if (numFrames < 1000) imgFileFormatStr = "{}/{}{:03d}.png";
	else if (numFrames < 10000) imgFileFormatStr = "{}/{}{:04d}.png";
	else if (numFrames < 100000) imgFileFormatStr = "{}/{}{:05d}.png";
	else if (numFrames < 1000000) imgFileFormatStr = "{}/{}{:06d}.png";
	else imgFileFormatStr = "{}/{}{:07d}.png";
}


void exportAllImages(vector<Mat>& frames) {
	printf("\nSaving all %d images.\n", numFrames);	
	for (int i = 0; i < numFrames; ++i) {
		string img_path = fmt::format(imgFileFormatStr, outputFolder, seriesName, i);
		imwrite(img_path, frames.at(i));
		if (i % 100 == 0)  // Print a dot for each 100 images saved.
			printf(".");
	}
	cout << "\nSaving all images DONE\n";
}


/// <summary>
///  Export a saved image sequence to a video. The method loads images from storage and
///    put them together as a video.
/// </summary>
/// <param name="frames">Reference to the frame buffer vector.</param>
/// <param name="numFrames">The number of frames in the recording sequence.</param>
/// <param name="framesPerSec">Frame rate (frames per second, fps).</param>
void exportVideo(vector<Mat>& frames, const int numFrames, const int framesPerSec) {
	cout << "\nExporting a video from a saved image sequence." << endl;
	double t0 = omp_get_wtime();
	printf("Frame size (width, height) = (%d, %d)\n", frameWidth, frameHeight);
	string videoPath = outputFolder + "/" + seriesName + ".avi";
	VideoWriter vidWriter(videoPath,
		cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
		framesPerSec, cv::Size(frameWidth, frameHeight), true);

	Mat vidFrame = cv::Mat(frameHeight, frameWidth, CV_8UC3);
	for (int frameID = 0; frameID < numFrames; ++frameID) {
		vidWriter << cv::imread(fmt::format(imgFileFormatStr, outputFolder, seriesName, frameID));
		if (frameID % 100 == 0)  // Print a dot for each 100 images saved.
			printf(".");
	}
	fmt::print("\nExporting a video DONE, {:.2f} seconds\n", omp_get_wtime() - t0);
}


/// <summary>
/// The first few frames of grabbing and retrieving usually involves many initialization
///   process. It will be more time consuming than usual and frame times significantly vary. 
///   Therefore, we will drop a few frames, say 5, and start the process from the sixth.
/// </summary>
/// <param name="cap">Shared pointer to the capturing device.</param>
/// <param name="dummyFrame">A dummy frame for data retrieval. 
///   You may use the first frame in the buffer for this.</param>
void warmUpGrabbingAndRetrieving(shrptr_VideoCapture cap, Mat dummyFrame) {
	for (int i = 0; i < 5; ++i) {
		cap->grab();
		cap->retrieve(dummyFrame);  // This dummy frame will be overwriten by a real frame.
	}
}


/// <summary>
/// Report grabbing times and their deviation from the ideal ones of all frames.
/// </summary>
/// <param name="numFrames">The number of frames in the recording sequence.</param>
/// <param name="idealTimeBetweenFrames">Ideal time between two consecutive frames.</param>
/// <param name="grabTimeStamps">Reference to a vector storing time stamp for each frame grabbing.</param>
/// <param name="waitTimes">Reference to a vector storing wait time for each frame.</param>
void reportGrabTimeAndDeviation(const int numFrames, const double idealTimeBetweenFrames,
		vector<double>& grabTimeStamps, vector<int>& waitTimes) {
	string reportPath = outputFolder + "/" + timeDeviationReportFileName;
	cout << "\nSaving deviation of frame arrival time to " << reportPath << endl;
	ofstream reportFile(reportPath);
	reportFile << "FrameID\t" << "FrameTime(ms)\t" << "WaitTime(ms)\t" << "ArrivalTimeDeviation(ms)\n";

	double previousTimeStamp = 0;
	double timeDiffSum = 0;
	for (int frameID = 0; frameID < numFrames; ++frameID) {
		double grabTime = grabTimeStamps.at(frameID);
		double frameStartTime = idealTimeBetweenFrames * frameID;
		double expectedTime = (idealTimeBetweenFrames * (frameID + 1));
		double timeDiff = (grabTime - expectedTime) * 1000;
		reportFile << fmt::format("{:3d}\t{:5.2f}\t{:2d}\t{:.2f}\n", frameID + 1, 
			(grabTime - frameStartTime) * 1000, waitTimes.at(frameID), timeDiff);
		timeDiffSum += abs(timeDiff);
		previousTimeStamp = grabTimeStamps.at(frameID);
		if (frameID % 100 == 0)  // Print a dot for every 100 lines saved.
			cout << ".";
	}
	reportFile << fmt::format("\nTotal absolute deviation time = {:.2f} ms, average absolute deviation time = {:.3f} ms\n",
		timeDiffSum, timeDiffSum / numFrames);
	reportFile.close();
	cout << "\nSaving time deviation DONE" << endl;
	fmt::print("\nTotal absolute deviation time = {:.2f} ms, average absolute deviation time = {:.3f} ms\n",
		timeDiffSum, timeDiffSum / numFrames);
}


/// <summary>
/// The loop of a video capture thread performing three main tasks: frame grabbing, pushing grabbed frame to 
///   the buffer, and waiting for an ideal frame grabbing time.
/// </summary>
/// <param name="cap">Shared pointer to the capturing device.</param>
/// <param name="frames"></param>
/// <param name="numFrames">The number of frames in the recording sequence.</param>
/// <param name="idealTimeBetweenFrames">Ideal time between two consecutive frames.</param>
/// <param name="grabTimeStamps">Pointer to a vector storing frame grabbing time stamps.</param>
/// <param name="retrieveTimeStamps">Pointer to a vector storing frame retrieval time stamps.</param>
/// <param name="waitTimes">Pointer to a vector storing wait time for each frame.</param>
void grabPushWaitThdLoop(shrptr_VideoCapture cap, vector<Mat>* frames, 
		const int numFrames, const double idealTimeBetweenFrames, 
		vector<double>* grabTimeStamps, vector<double>* retrieveTimeStamps, 
		vector<int>* waitTimes) {
	double time0 = omp_get_wtime();
	for (int frameID = 0; frameID < numFrames; ++frameID) {
		// Video frame is captured when grab is called. So, we compute the wait time
		//   right before we call grab.	For example, at 30 fps, the first frame should be captured
		//   at about t = 0.0333 second.
		int waitTime = waitForNextGrab(frameID + 1, idealTimeBetweenFrames, time0);
		const double grabTimeStamp = omp_get_wtime() - time0;
		cap->grab();  // Video frame is stored in a buffer, waiting for retrieval to RAM.
		grabTimeStamps->push_back(grabTimeStamp);
		waitTimes->push_back(waitTime);
		double t = pushFrameToMatCircularBuffer(cap, time0, *frames, frameID);
		retrieveTimeStamps->push_back(t);
	}
	fmt::print("Frame grapping DONE, {:.2f} seconds\n", omp_get_wtime() - time0);
}


/// <summary>
/// The core mechanism of VidCap Pacer.
/// </summary>
/// <param name="cap">Shared pointer to the capturing device.</param>
/// <param name="idealTimeBetweenFrames">Ideal time between two consecutive frames.</param>
/// <param name="numFrames">The number of frames in the recording sequence.</param>
/// <param name="framesPerSec">Frame rate (frames per second, fps)</param>
void captureToMemorySpace(shrptr_VideoCapture cap, const double idealTimeBetweenFrames,
	const int numFrames, const int framesPerSec) {
	vector<Mat> frames(ioBufferLength);  // Use parameter numFrames if all to be stored.
	const int frameHeight = (int)cap->get(cv::CAP_PROP_FRAME_HEIGHT);
	const int frameWidth = (int)cap->get(cv::CAP_PROP_FRAME_WIDTH);
	prepareBufferFrames(frames, frameHeight, frameWidth);
	warmUpGrabbingAndRetrieving(cap, frames.at(0));
	
	vector<double> grabTimeStamps;
	vector<double> retrieveTimeStamps;
	vector<int> waitTimes;
	
	// Start a frame grabbing thread 
	std::thread grabThread(grabPushWaitThdLoop, cap, &frames, numFrames, 
		idealTimeBetweenFrames, &grabTimeStamps, &retrieveTimeStamps, &waitTimes);

	// Start a thread for saving video frames if I/O buffer cannot contain the entire expected sequence.
	if (numFrames > ioBufferLength) {
		std::thread frameSavingThread(saveFramesThd, &frames);
		frameSavingThread.join();
	}	

	grabThread.join();

	reportTimeStamps(grabTimeStamps, retrieveTimeStamps);

	// If the buffer can hold the entire set of grabbed frames, we will write the frames
	//   when all frames are available in the buffer. The I/O thread is not created in this case.
	if (numFrames <= ioBufferLength) {
		exportAllImages(frames);
	}

	if(videoExport)
		exportVideo(frames, numFrames, framesPerSec);

	reportGrabTimeAndDeviation(numFrames, idealTimeBetweenFrames, grabTimeStamps, waitTimes);
}

