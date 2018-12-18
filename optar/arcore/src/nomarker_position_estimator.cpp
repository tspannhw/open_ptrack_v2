
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <ros/ros.h>
#include <opt_msgs/ArcoreCameraImage.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/xphoto.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/features2d/features2d.hpp>
#include <chrono>
#include <tf/transform_listener.h>
#include <Eigen/Dense>
#include <opencv2/calib3d.hpp>
#include <opencv2/core/eigen.hpp>
#include <thread>         // std::this_thread::sleep_for
#include <visualization_msgs/Marker.h>
#include <tf/transform_broadcaster.h>

#define ARCORE_CAMERA_IMAGE_REPUBLISHER_NODE_NAME "nomarker_position_estimator"

tf::StampedTransform transformKinectToWorld;
ros::Publisher pose_raw_pub;
ros::Publisher pose_marker_pub;

int publish_marker(std_msgs::Header header, float tx, float ty, float tz, float qx, float qy, float qz, float qw)
{
	visualization_msgs::Marker marker_pose;

	marker_pose.header.frame_id = "world";
	marker_pose.ns = "no_marker_raw";
	marker_pose.type = visualization_msgs::Marker::SPHERE;
	marker_pose.action = visualization_msgs::Marker::ADD;
	marker_pose.scale.x = 0.2;
	marker_pose.scale.y = 0.2;
	marker_pose.scale.z = 0.2;
	marker_pose.color.a = 1.0;
	marker_pose.color.r = 1;//float(rand()*256) / 255;
	marker_pose.color.g = 0;//float(rand()*256) / 255;
	marker_pose.color.b = 0;//float(rand()*256) / 255;
	marker_pose.lifetime = ros::Duration(10);


	marker_pose.header.stamp = header.stamp;
	marker_pose.id = header.seq;
	marker_pose.pose.position.x = tx;
	marker_pose.pose.position.y = ty;
	marker_pose.pose.position.z = tz;
	marker_pose.pose.orientation.x = qx;
	marker_pose.pose.orientation.y = qy;
	marker_pose.pose.orientation.z = qz;
	marker_pose.pose.orientation.w = qw;

  	pose_marker_pub.publish(marker_pose);


	tf::Vector3 tran_input;
	tf::Quaternion quat_input((qx),qy,qz,qw);
	tran_input.setX(tx);
	tran_input.setY(ty);
	tran_input.setZ(tz);
	
	static tf::TransformBroadcaster br;

	tf::Transform transformToSend;
	transformToSend.setOrigin(tran_input);
	transformToSend.setRotation(quat_input);

	br.sendTransform(tf::StampedTransform(transformToSend, header.stamp, "world", "mobile_camera"));

  	return 0;
}

int findOrbMatches(	const cv::Mat& arcoreImg, 
					const cv::Mat& kinectCameraImg, 
					std::vector<cv::DMatch>& matches, 
					std::vector<cv::KeyPoint>& arcoreKeypoints, 
					std::vector<cv::KeyPoint>& kinectKeypoints)
{
	matches.clear();
    cv::Ptr<cv::ORB> orb = cv::ORB::create();

    //detect keypoints on both images
   	std::chrono::steady_clock::time_point beforeDetection = std::chrono::steady_clock::now();
    orb->detect(kinectCameraImg, kinectKeypoints);
    orb->detect(arcoreImg, arcoreKeypoints);
   	std::chrono::steady_clock::time_point afterDetection = std::chrono::steady_clock::now();
    unsigned long detectionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(afterDetection - beforeDetection).count();
	ROS_INFO("Keypoints detection took %lu ms",detectionDuration);
	ROS_INFO_STREAM("Found "<<arcoreKeypoints.size()<<" keypoints for arcore, "<<kinectKeypoints.size()<<" for kinect");

	if ( kinectKeypoints.empty() )
	{
		ROS_ERROR("No keypoints found for kinect");
		return -1;
	}
	if ( arcoreKeypoints.empty() )
	{
		ROS_ERROR("No keypoints found for arcore");
		return -2;
	}

	//compute descriptors for the keypoints in both images
   	std::chrono::steady_clock::time_point beforeDescriptors = std::chrono::steady_clock::now();
    cv::Mat kinectDescriptors;
    cv::Mat arcoreDescriptors;
    orb->compute(kinectCameraImg,kinectKeypoints,kinectDescriptors);
	orb->compute(arcoreImg,arcoreKeypoints,arcoreDescriptors);
   	std::chrono::steady_clock::time_point aftereDescriptors = std::chrono::steady_clock::now();
	unsigned long descriptorsDuration = std::chrono::duration_cast<std::chrono::milliseconds>(aftereDescriptors - beforeDescriptors).count();
	ROS_INFO("Descriptors computation took %lu ms",descriptorsDuration);

	if ( kinectDescriptors.empty() )
	{
		ROS_ERROR("No descriptors for kinect");
		return -3;
	}
	if ( arcoreDescriptors.empty() )
	{
		ROS_ERROR("no descriptors for arcore");
		return -4;
	}
  	 
  	//find matches between the descriptors
   	std::chrono::steady_clock::time_point beforeMatching = std::chrono::steady_clock::now();
    cv::BFMatcher matcher(cv::NORM_HAMMING);
    matcher.match(arcoreDescriptors,kinectDescriptors,matches);
   	std::chrono::steady_clock::time_point afterMatching = std::chrono::steady_clock::now();
	unsigned long matchingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(afterMatching - beforeMatching).count();
	ROS_INFO("Descriptors matching took %lu ms",matchingDuration);
	return 0;
}

int filterMatches(const std::vector<cv::DMatch>& matches, std::vector<cv::DMatch>& goodMatches)
{
	double max_dist = -10000000;
  	double min_dist = 10000000;

	//Find minimum and maximum distances between matches
	for( unsigned int i = 0; i < matches.size(); i++ )
	{
		double dist = matches[i].distance;
		if( dist < min_dist )
			min_dist = dist;
		if( dist > max_dist )
			max_dist = dist;
	}
	ROS_INFO("Max match dist : %f \n", max_dist );
	ROS_INFO("Min match dist : %f \n", min_dist );

	//Filter the matches to keep just the best ones
	for( unsigned int i = 0; i < matches.size(); i++ )
	{
		if( matches[i].distance <= std::min(min_dist+(max_dist-min_dist)*0.2,20.0) )
		{
			goodMatches.push_back( matches[i]);
			//ROS_INFO_STREAM("got a good match, dist="<<matches[i].distance);
		}
	}
	return 0;
}


cv::Point3f get3dPoint(int x, int y, const cv::Mat& cameraImage, const cv::Mat& depthImage, double focalLengthX, double focalLengthY, double principalPointX, double principalPointY)
{
	cv::Point3f p;
	p.z = (float)(((double)depthImage.at<uint16_t>(x,y))/1000);//convert to meters
	p.x = (x - principalPointX) * p.z / focalLengthX;
	p.y = (y - principalPointY) * p.z / focalLengthY;
	return p;
}

cv::Mat DoubleMatFromVec3b(cv::Vec3b in)
{
    cv::Mat mat(3,1, CV_64FC1);
    mat.at <double>(0,0) = in [0];
    mat.at <double>(1,0) = in [1];
    mat.at <double>(2,0) = in [2];

    return mat;
};


void getEigenPose(cv::Vec3d tvecV, cv::Vec3d rvecV, Eigen::Vector3d &Translate, Eigen::Quaterniond &quats)
{
    cv::Mat R;
    cv::Mat tvec, rvec;

    tvec = DoubleMatFromVec3b(tvecV);
    rvec = DoubleMatFromVec3b(rvecV);

    cv::Rodrigues(rvec, R); // R is 3x3
    R = R.t();                 // rotation of inverse
    tvec = -R*tvec;           // translation of inverse

    Eigen::Matrix3d mat;
    cv2eigen(R, mat);

    Eigen::Quaterniond EigenQuat(mat);

    quats = EigenQuat;


    double x_t = tvec.at<double>(0, 0);
    double y_t = tvec.at<double>(1, 0);
    double z_t = tvec.at<double>(2, 0);

    Translate.x() = x_t * 10;
    Translate.y() = y_t * 10;
    Translate.z() = z_t * 10;   

}


void imagesCallback(const opt_msgs::ArcoreCameraImageConstPtr& arcoreInputMsg,
					const sensor_msgs::ImageConstPtr& kinectInputCameraMsg,
					const sensor_msgs::ImageConstPtr& kinectInputDepthMsg,
					const sensor_msgs::CameraInfo& kinectCameraInfo)
{
	std::chrono::steady_clock::time_point beginning = std::chrono::steady_clock::now();
	long arcoreTime = arcoreInputMsg->header.stamp.sec*1000000000L + arcoreInputMsg->header.stamp.nsec;
	long kinectTime = kinectInputCameraMsg->header.stamp.sec*1000000000L + kinectInputCameraMsg->header.stamp.nsec;
	ROS_INFO("received images. time diff = %+7.5f sec.  arcore time = %012ld  kinect time = %012ld",(arcoreTime-kinectTime)/1000000000.0, arcoreTime, kinectTime);

	cv::Mat arcoreImg = cv::imdecode(cv::Mat(arcoreInputMsg->image.data),1);//convert compressed image data to cv::Mat
	if(!arcoreImg.data)
	{
		ROS_ERROR("couldn't decode arcore image");
		return;
	}
    //cv::imshow("Arcore raw", arcoreImg);
	if(arcoreImg.channels()!=3)
	{
		ROS_ERROR("Color image expected from arcore device, received something different");
		return;
	}
	cv::Mat planes[3];
	split(arcoreImg,planes);  // planes[2] is the red channel
	arcoreImg = planes[2];
	cv::Mat flippedArcoreImg;
	cv::flip(arcoreImg,flippedArcoreImg,0);
	arcoreImg=flippedArcoreImg;
	//cv::xphoto::createSimpleWB()->balanceWhite(flippedArcoreImg,arcoreImg);
	//cv::equalizeHist(flippedArcoreImg,arcoreImg);
    ROS_INFO("decoded arcore image");
    //cv::imshow("Arcore", arcoreImg);


	cv::Mat kinectCameraImg = cv_bridge::toCvShare(kinectInputCameraMsg)->image;//convert compressed image data to cv::Mat
	if(!kinectCameraImg.data)
	{
		ROS_ERROR("couldn't extract kinect camera opencv image");
		return;
	}
    ROS_INFO("decoded kinect camera image");
    //cv::imshow("Kinect", kinectCameraImg);


	cv::Mat kinectDepthImg = cv_bridge::toCvShare(kinectInputDepthMsg)->image;//convert compressed image data to cv::Mat
	if(!kinectDepthImg.data)
	{
		ROS_ERROR("couldn't extract kinect depth opencv image");
		return;
	}
    ROS_INFO("decoded kinect depth image");

	std::chrono::steady_clock::time_point afterDecoding = std::chrono::steady_clock::now();
	unsigned long decodingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(afterDecoding - beginning).count();
	ROS_INFO("Images decoding and initialization took %lu ms",decodingDuration);






	//find matches
    std::vector<cv::DMatch> matches;
    std::vector<cv::KeyPoint>  arcoreKeypoints;
    std::vector<cv::KeyPoint>  kinectKeypoints;
    int r = findOrbMatches(arcoreImg, kinectCameraImg, matches, arcoreKeypoints, kinectKeypoints);
    if(r<0)
    {
    	ROS_ERROR("error finding matches");
    	return;
    }
  	ROS_INFO_STREAM("got "<<matches.size()<<" matches");

    //filter matches
	std::vector< cv::DMatch > goodMatches;
  	r = filterMatches(matches,goodMatches);
  	if(r<0)
  	{
  		ROS_ERROR("error filtering matches");
  		return;
  	}
  	ROS_INFO_STREAM("got "<<goodMatches.size()<<" good matches");




    cv::Mat matchesImg;
    cv::drawMatches(arcoreImg, arcoreKeypoints, kinectCameraImg, kinectKeypoints, goodMatches, matchesImg, cv::Scalar::all(-1), cv::Scalar::all(-1), std::vector<char>(), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);

	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	unsigned long totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - beginning).count();
	ROS_INFO("total duration is %lu ms",totalDuration);


    cv::imshow("Matches",matchesImg);
    cv::waitKey(10);


    //find the 3d poses corrseponding to the goodMatches, these will be relative to the kinect frame
    std::vector<cv::Point3f> goodMatches3dPos;
    std::vector<cv::Point2f> goodMatchesImgPos;
	for( unsigned int i = 0; i < goodMatches.size(); i++ )
	{
		cv::Point2f imgPos = kinectKeypoints.at(goodMatches.at(i).trainIdx).pt;//queryIdx means in the kinect descriptors as we passed to the matcher as the train set
		goodMatchesImgPos.push_back(imgPos);
		cv::Point3f pos3d = get3dPoint(	imgPos.x,imgPos.y,
												kinectCameraImg,
												kinectDepthImg,
												kinectCameraInfo.P[0+4*0],kinectCameraInfo.P[1+4*1],kinectCameraInfo.P[2+4*0],kinectCameraInfo.P[2+4*1]);
		ROS_INFO_STREAM("3d pose = "<<pos3d.x<<";"<<pos3d.y<<";"<<pos3d.z);
		goodMatches3dPos.push_back(pos3d);
	}

	if(goodMatches.size()<4)
	{
		ROS_INFO("not enough matches to determine position");
		return;
	}

	double projMat[kinectCameraInfo.P.size()];
	std::copy(kinectCameraInfo.P.begin(), kinectCameraInfo.P.end(), projMat);
	cv::Mat cameraMatrix = cv::Mat(3, 3, CV_32FC1, projMat);
	cv::Mat tvec;
	cv::Mat rvec;
	cv::solvePnPRansac(goodMatches3dPos,goodMatchesImgPos,cameraMatrix,cv::noArray(),tvec,rvec);
	
	Eigen::Vector3d position;
	Eigen::Quaterniond rotation;
	getEigenPose(tvec,rvec,position,rotation);

	geometry_msgs::PoseStamped pose;
	pose.pose.position.x = position.x();
	pose.pose.position.y = position.y();
	pose.pose.position.z = position.z();

	pose.pose.orientation.x = rotation.x();
	pose.pose.orientation.y = rotation.y();
	pose.pose.orientation.z = rotation.z();
	pose.pose.orientation.w = rotation.w();

	geometry_msgs::TransformStamped transformMsg;
	//tf2::convert(transformKinectToWorld, transformMsg);
	tf::transformStampedTFToMsg(transformKinectToWorld,transformMsg);

	tf2::doTransform(pose,pose,transformMsg);

	pose.header.frame_id = "/world";
	pose.header.stamp = arcoreInputMsg->header.stamp;

	ROS_INFO_STREAM("estimated pose is "<<pose.pose.position.x<<" "<<pose.pose.position.y<<" "<<pose.pose.position.z<<" ; "<<pose.pose.orientation.x<<" "<<pose.pose.orientation.y<<" "<<pose.pose.orientation.z<<" "<<pose.pose.orientation.w);

	pose_raw_pub.publish(pose);
	publish_marker(pose.header, pose.pose.position.x, pose.pose.position.y, pose.pose.position.z, pose.pose.orientation.x, pose.pose.orientation.y, pose.pose.orientation.z, pose.pose.orientation.w);
}


int main(int argc, char** argv)
{
	ros::init(argc, argv, ARCORE_CAMERA_IMAGE_REPUBLISHER_NODE_NAME);
	ros::NodeHandle nh;
	ROS_INFO_STREAM("starting "<<ARCORE_CAMERA_IMAGE_REPUBLISHER_NODE_NAME);

	boost::shared_ptr<sensor_msgs::CameraInfo const> kinectCameraInfoPtr;
	sensor_msgs::CameraInfo kinectCameraInfo;
	ROS_INFO("waiting for kinect camera_info");
	kinectCameraInfoPtr = ros::topic::waitForMessage<sensor_msgs::CameraInfo>("kinect_camera_info");
	if(kinectCameraInfoPtr == NULL)
	{
		ROS_ERROR("couldn't get kinect's camera_info, did the node shut down? I'll shut down.");
		return 0;
	}
	kinectCameraInfo = *kinectCameraInfoPtr;


	ros::Time targetTime;
	tf::TransformListener listener;
	std::string inputFrame = kinectCameraInfo.header.frame_id;
	std::string targetFrame = "/world";
	ros::Duration timeout = ros::Duration(10.0);
	bool retry=true;
	int count=0;
	ROS_INFO_STREAM("getting transform from "<<inputFrame<<" to "<<targetFrame);
	std::this_thread::sleep_for (std::chrono::seconds(1));//sleep one second to let tf start
	do
	{
		std::string failReason;
		targetTime = ros::Time::now();
		bool r = listener.waitForTransform( targetFrame,inputFrame, targetTime, timeout, ros::Duration(0.01),&failReason);
		if(!r)
		{
			ROS_INFO_STREAM("can't transform because: "<<failReason);
			if(count>10)
				return -1;
			ROS_INFO("retrying");
		}
		else
			retry=false;
		count++;
	}while(retry);
	ROS_INFO("got transform");
	transformKinectToWorld = tf::StampedTransform();
	listener.lookupTransform(targetFrame, inputFrame, targetTime, transformKinectToWorld);
	//ROS_INFO("got transform from %s to %s ",inputFrame.c_str(), targetFrame.c_str());


	pose_raw_pub = nh.advertise<geometry_msgs::PoseStamped>("optar/no_marker_pose_raw", 10);
	pose_marker_pub = nh.advertise<visualization_msgs::Marker>("optar/pose_raw_marker", 1);



	message_filters::Subscriber<opt_msgs::ArcoreCameraImage> arcoreCamera_sub(nh, "arcore_camera", 1);
	message_filters::Subscriber<sensor_msgs::Image> kinect_img_sub(nh, "kinect_camera", 1);
	message_filters::Subscriber<sensor_msgs::Image> kinect_depth_sub(nh, "kinect_depth", 1);
	
	// Synchronization policy for having a callback that receives two topics at once.
	// It chooses the two messages by minimizing the time difference between them
	typedef message_filters::sync_policies::ApproximateTime<opt_msgs::ArcoreCameraImage, sensor_msgs::Image, sensor_msgs::Image> MyApproximateSynchronizationPolicy;

	//instantiate and set up the policy
	MyApproximateSynchronizationPolicy policy = MyApproximateSynchronizationPolicy(60);//instatiate setting up the queue size
	//we know we will receive stuff from arcore once per second, so we tell the algorithm a lower bound of
	// half a second, as suggested by the authors. This should make it more effective
	policy.setInterMessageLowerBound(0,ros::Duration(0,500000000));

	//Instantiate a Synchronizer with our policy.
	message_filters::Synchronizer<MyApproximateSynchronizationPolicy>  sync(MyApproximateSynchronizationPolicy(policy), arcoreCamera_sub, kinect_img_sub, kinect_depth_sub);

	//registers the callback
	auto f = boost::bind(&imagesCallback, _1, _2, _3, kinectCameraInfo);

	ROS_INFO("waiting for images");
	sync.registerCallback(f);



	ros::spin();

	return 0;
}
