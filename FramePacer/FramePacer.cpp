// FramePacer.cpp : This file contains the 'main' function. Program execution begins and ends there.
// Build OpenCV for VC 17 (Visual Studio 2022): https://www.gollahalli.com/blog/build-opencv-with-visual-studio-and-cmake-gui/#step-4-set-the-source-and-build-directories
// Remember to check option build opencv_world in CMake before you generate the solution.

// TODO: Parameterize output folders (lossless frames and videos)
// TODO: Use I/O thread
// TODO: Save videos at interval

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

#define shrptr_VideoCapture std::shared_ptr<cv::VideoCapture> 

using namespace cv;
using namespace std;

shrptr_VideoCapture initVideoCapture(const int camID, const int frameHeight = 480,
	const int frameWidth = 640, const double fps=30);

void captureToMemorySpace(shrptr_VideoCapture cap, const double timeBetweenFrames,
	const int numFrames, const int framesPerSec);

void checkTargetFpsAgainstActualFps(const double targetFPS, const double actualFPS);

void setImgFileNameFormatString(const int numFrames);

/* Program arguments :
	1. camID (int): Camera ID (video capture device) of the machine.
	2. frameHeight (int): a height of each video frame
	3. frameWidth (int): a width of each video frame
	4. targetFPS (double): a target frame rate (frames per second). 
		Note that your video capture device may not obey this frame rate, but this tool 
		tries to time each frame capture at a expected time stamp as precisely as possible.
		As long as the target frame rate and resolution are not too high, this tool may
		be able to achieve the target frame rate we specify.
	5. numSeconds: duration of video capturing in second unit.
	6. I/O buffer size: the number of video frames buffered before writing to storage. 
	    If your system is highly affected by I/O and cannot keep frame interval as precise
		  as you need because of I/O, you may increase this size high enough to cover all
		  frames that will be captured during the time interval. Then, set 'saveOnce' to true.
	    Note: the I/O itself may not time consuming per se, but encoding an output image may
		  be a culprit if you get an I/O performance issue.
	. dryRun (boolean): if true, captureed video frames will not be saved to a disk. 
	    This helps investigate performance issues, especially those involving I/O.
	. saveVideo (boolean):
	. saveLosslessFrames (boolean):
	. saveOnce (boolean):
*/

string series_name = "demo_";
string img_out_folder = "C:/TestingGround/VideoCapture/Images";
string imgFileFormatStr;
string video_out_folder = "C:/TestingGround/VideoCapture/Videos";
string timeStampReportFileName = "time_stamp_report.tab";
string timeDeviationReportFileName = "time_deviation_report.tab";
int ioBufferSize = 30;
std::atomic<int> bufferEndIndex(0);
std::atomic<int> bufferStartIndex(0);
int framesNotWritten = 0;
int framesLeftToCapture = 0;
std::mutex writeMutex;
int timeBetweenFramesMSec;
cv::Mat saveBuffer;
int numFrames;

std::atomic<int> grabCount(0);
std::atomic<int> writeCount(0);


int main() {
    std::cout << "Start of Video Capturing\n";
	const int camID = 0;
	const int frameHeight = 480;
	const int frameWidth = 640;
	const double targetFPS = 15;
	cout << "Target frame rate = " << targetFPS << "\n";
	shrptr_VideoCapture cap = initVideoCapture(camID, frameHeight, frameWidth, targetFPS);
	
	const int numSeconds = 5;
	numFrames = (int)(targetFPS * numSeconds);
	cout << "Number of frames = " << numFrames << "\n";
	framesLeftToCapture = numFrames;
	setImgFileNameFormatString(numFrames);
	cap->set(cv::CAP_PROP_FPS, targetFPS);
	double actualFPS = cap->get(cv::CAP_PROP_FPS);
	
	const double timeBetweenFrames = 1.0 / targetFPS;
	timeBetweenFramesMSec = (int)(timeBetweenFrames * 1000);
	cout << "Time between frames = " << timeBetweenFramesMSec << " msec\n";

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
/// <param name="targetFPS"></param>
/// <param name="actualFPS"></param>
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


/// To pace frame arrival, we compute the time our application should wait before
///   issuing the next frame grab command.
/// The wait time end a little bit earlier by marginTime.
/// The early end of wait time is to compensate the incoming work before the actual
///   video capturing. We should calibrate the marginTime to make frame pacing close
///   to the ideal as much as possible.
int waitForNextGrab(int nextFrameID, double timeBetweenFrame, double time0) {
	double currTime = omp_get_wtime();
	double elapsedTime = currTime - time0;
	double nextTime = nextFrameID * timeBetweenFrame;

	// Capture thread will awake before the expected capture time by the amount of
	//   margin time. For example, if marginTime is 0.002, the thread will awake 2 ms
	//   before the expected capture time.
	const double marginTime = 0.003;  
	int waitTime = -1;
	if (elapsedTime < nextTime - marginTime) { // Need to wait until the next time
		waitTime = (int)((nextTime - elapsedTime - marginTime) * 1000);
		if (waitTime > 0)
			Sleep(waitTime);
	}
	return waitTime;
}


void prepareEmptyFrames(std::vector<cv::Mat>& frames, const int height,
		const int width) {
	saveBuffer = cv::Mat(height, width, CV_8UC3);
	for (int frameID = 0; frameID < ioBufferSize; ++frameID) {
		frames.at(frameID) = cv::Mat(height, width, CV_8UC3);
	}
}


/// Retrieve a frame and store it in a frame array. Then, compute an elapsed time
///   based on the reference time0.
/// @return elapsed time from the beginning of processing (time0).
double pushFrameToMat(shrptr_VideoCapture cap, double time0, vector<Mat>& frames, 
		int frameID) {
	cap->retrieve(frames.at(frameID));

	double currTime = omp_get_wtime();
	double elapsedTime = currTime - time0;	// record how much time passed (milli-second)
	int elapsedSecond = (int)elapsedTime;	// record how much time passed (second, drop fractional value).
	return elapsedTime;
}


void writeFrameToImageFile(int frameID, vector<Mat>* frames) {
	//std::lock_guard<std::mutex> lock(*mutexVid);
	string imgPath = fmt::format(imgFileFormatStr, img_out_folder, series_name, frameID);

	writeMutex.lock(); {
		saveBuffer = frames->at(bufferStartIndex);
		bufferStartIndex = (bufferStartIndex + 1) % ioBufferSize;
		framesNotWritten -= 1;
	}
	writeMutex.unlock();

	imwrite(imgPath, saveBuffer);
}


double pushFrameToMatCircularBuffer(shrptr_VideoCapture cap, double time0,
		vector<Mat>& frames, int frameID) 
{
	writeMutex.lock(); {
		if ((bufferEndIndex + 1) % ioBufferSize == bufferStartIndex) {
			printf("Error: I/O buffer is full.\n");
			exit(0);
		}
		bufferEndIndex = (bufferEndIndex + 1) % ioBufferSize;
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


// Report the time stamp of each frame to a file.
void reportTimeStamps(vector<double>& grabTime, vector<double>& retrieveTime) {
	string timeStampPath = img_out_folder + "/" + timeStampReportFileName;
	cout << "Saving the time stamp of each frame to " << timeStampPath << "\n";
	ofstream reportFile(timeStampPath);

	reportFile << "FrameID\tGrabTime(s)\tRetrievalTime(s)\n";
	for (int i = 0; i < grabTime.size(); ++i) {
		reportFile << fmt::format("{}\t{}\t{}\n", i + 1, grabTime.at(i), retrieveTime.at(i));
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
	printf("Saving all %d images.\n", numFrames);	
	for (int i = 0; i < numFrames; ++i) {
		string img_path = fmt::format(imgFileFormatStr, img_out_folder, series_name, i);
		imwrite(img_path, frames.at(i));
		if (i % 100 == 0)  // Print a dot for each 100 images saved.
			printf(".");
	}
	cout << "\nSaving all images DONE\n";
}


void exportVideo(vector<Mat>& frames, const int numFrames, const int framesPerSec) {
	int width = frames.at(0).cols;
	int height = frames.at(0).rows;
	printf("Frame size = (%d, %d)\n", width, height);
	VideoWriter vidWriter("C:/TestingGround/VideoCapture/test.avi",
		cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
		framesPerSec, cv::Size(width, height), true);
	for (int frameID = 0; frameID < numFrames; ++frameID) {
		vidWriter << frames.at(frameID);
	}
}


// The first few frames of grabbing and retrieving usually involves many initialization 
//   process.  Therefore, it will be more time consuming than usual. We will drop a few 
//   frames, say 5, and start the process from the sixth.
void warmUpGrabbingAndRetrieving(shrptr_VideoCapture cap, Mat dummyFrame) {
	for (int i = 0; i < 5; ++i) {
		cap->grab();
		cap->retrieve(dummyFrame);  // This dummy frame will be overwriten by a real frame.
	}
}


void reportGrabTimeAndDeviation(const int numFrames, const double timeBetweenFrames,
		vector<double>& grabTimeStamps, vector<int>& waitTimes) {
	string reportPath = img_out_folder + "/" + timeDeviationReportFileName;
	cout << "Saving deviation of frame arrival time to " << reportPath << endl;
	ofstream reportFile(reportPath);
	reportFile << "FrameID\t" << "FrameTime(ms)\t" << "WaitTime(ms)\t" << "ArrivalTimeDeviation(ms)\n";

	double previousTimeStamp = 0;
	int timeDiffSum = 0;
	for (int frameID = 0; frameID < numFrames; ++frameID) {
		double grabTime = grabTimeStamps.at(frameID);
		double frameStartTime = timeBetweenFrames * frameID;
		double expectedTime = (timeBetweenFrames * (frameID + 1));
		double timeDiff = (grabTime - expectedTime) * 1000;
		reportFile << fmt::format("{:3d}\t{:5.2f}\t{:2d}\t{:.2f}\n",
			frameID + 1, (grabTime - frameStartTime) * 1000, waitTimes.at(frameID), 
			timeDiff);
		timeDiffSum += abs(timeDiff);
		previousTimeStamp = grabTimeStamps.at(frameID);
		if (frameID % 100 == 0)  // Print a dot for every 100 lines saved.
			cout << ".";
	}
	reportFile << fmt::format("\nTotal absolute deviation time = {} ms, average absolute deviation time = {:.3f} ms\n",
		timeDiffSum, timeDiffSum / (double)numFrames);
	reportFile.close();
	cout << "\nSaving time deviation DONE" << endl;
	printf("Total absolute deviation time = %d ms, average absolute deviation time = %.3f ms\n",
		timeDiffSum, timeDiffSum / (double)numFrames);
}


void grabPushWaitThdLoop(shrptr_VideoCapture cap, vector<Mat>* frames, 
		const int numFrames, const double timeBetweenFrames, 
		vector<double>* grabTimeStamps, vector<double>* retrieveTimeStamps, 
		vector<int>* waitTimes)
{
	double time0 = omp_get_wtime();
	for (int frameID = 0; frameID < numFrames; ++frameID) {
		// Video frame is captured when grab is called. So, we compute the wait time
		//   right before we call grab.	For example, at 30 fps, the first frame should be captured
		//   at about t = 0.0333 second.
		int waitTime = waitForNextGrab(frameID + 1, timeBetweenFrames, time0);
		const double grabTimeStamp = omp_get_wtime() - time0;
		cap->grab();  // Video frame is stored in a buffer, waiting for retrieval to RAM.
		grabTimeStamps->push_back(grabTimeStamp);
		waitTimes->push_back(waitTime);
		double t = pushFrameToMatCircularBuffer(cap, time0, *frames, frameID);
		retrieveTimeStamps->push_back(t);
	}
	cout << "Frame grapping completed.\n";
}


void grabPushWaitLoop(shrptr_VideoCapture cap, vector<Mat>& frames, const int numFrames,
		const double timeBetweenFrames,	vector<double>& grabTimeStamps, 
		vector<double>& retrieveTimeStamps, vector<int>& waitTimes) 
{
	double time0 = omp_get_wtime();
	for (int frameID = 0; frameID < numFrames; ++frameID) {
		// Video frame is captured when grab is called. So, we compute the wait time
		//   right before we call grab.	For example, at 30 fps, the first frame should be captured
		//   at about t = 0.0333 second.
		int waitTime = waitForNextGrab(frameID + 1, timeBetweenFrames, time0);
		const double grabTimeStamp = omp_get_wtime() - time0;
		cap->grab();  // Video frame is stored in a buffer, waiting for retrieval to RAM.
		grabTimeStamps.push_back(grabTimeStamp);
		waitTimes.push_back(waitTime);
		//double t = pushFrameToMat(cap, time0, frames, frameID);
		double t = pushFrameToMatCircularBuffer(cap, time0, frames, frameID);
		retrieveTimeStamps.push_back(t);
	}
}


void captureToMemorySpace(shrptr_VideoCapture cap, const double timeBetweenFrames,
	const int numFrames, const int framesPerSec)
{
	vector<Mat> frames(ioBufferSize);  // Use parameter numFrames if all to be stored.
	const int frameHeight = (int)cap->get(cv::CAP_PROP_FRAME_HEIGHT);
	const int frameWidth = (int)cap->get(cv::CAP_PROP_FRAME_WIDTH);
	prepareEmptyFrames(frames, frameHeight, frameWidth);
	warmUpGrabbingAndRetrieving(cap, frames.at(0));
	
	vector<double> grabTimeStamps;
	vector<double> retrieveTimeStamps;
	vector<int> waitTimes;
	
	// Start a thread for frame grabbing
	std::thread grabThread(grabPushWaitThdLoop, cap, &frames, numFrames, 
		timeBetweenFrames, &grabTimeStamps, &retrieveTimeStamps, &waitTimes);

	// Start a thread for saving video frames if I/O buffer cannot contain all frames
	if (numFrames > ioBufferSize) {
		std::thread frameSavingThread(saveFramesThd, &frames);
		frameSavingThread.join();  // Synchronize within the loop
	}	

	grabThread.join();

	reportTimeStamps(grabTimeStamps, retrieveTimeStamps);

	// If the buffer can hold the entire set of grabbed frames, we will write the frames
	//   when all frames are available in the buffer.
	if (numFrames <= ioBufferSize) {
		exportAllImages(frames);
	}
	//exportVideo(frames, numFrames, framesPerSec);

	reportGrabTimeAndDeviation(numFrames, timeBetweenFrames, grabTimeStamps, waitTimes);
}

