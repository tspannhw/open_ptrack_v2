#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <Eigen/Dense>
#include <opencv2/core/eigen.hpp>
#include <visualization_msgs/Marker.h>
#include <opencv2/core/core.hpp>



//functions documentation in utils.cpp

cv::Point3f get3dPoint(int x, int y, int depth_mm, double focalLengthX, double focalLengthY, double principalPointX, double principalPointY);
cv::Mat DoubleMatFromVec3b(cv::Vec3b in);
void opencvPoseToEigenPose(cv::Vec3d rvec, cv::Vec3d tvec, Eigen::Vector3d &translation, Eigen::Quaterniond &quaternion);
int publish_pose_for_viewing(float tx, float ty, float tz, float qx, float qy, float qz, float qw, ros::Publisher pose_marker_pub, std::string name, float r, float g, float b, float a, float size);
double poseDistance(geometry_msgs::Pose pose1, geometry_msgs::Pose pose2);

visualization_msgs::Marker buildMarker(const geometry_msgs::Pose& pose, std::string name, float r, float g, float b, float a, float size, std::string frame_id);
visualization_msgs::Marker buildMarker(const cv::Point3f& position, std::string name, float r, float g, float b, float a, float size, std::string frame_id);

cv::Point2i findNearestNonZeroPixel(const cv::Mat& image, int x, int y, double maxDist);

void transformCvPoint3f(const cv::Point3f& in, cv::Point3f& out, tf::StampedTransform transform);
void prepareOpencvImageForShowing(std::string winName, cv::Mat image, int winHeight, int winWidth=-1);
void publishPoseAsTfFrame(const geometry_msgs::PoseStamped& pose, std::string tfFrameName);
void publishTransformAsTfFrame(const tf::Transform& transform, std::string tfFrameName, std::string parentFrame, const ros::Time& time);


geometry_msgs::Point buildRosPoint(double positionX, double positionY, double positionZ);
geometry_msgs::Quaternion& buildRosQuaternion(double quaternionX, double quaternionY, double quaternionZ, double quaternionW);
geometry_msgs::Pose buildRosPose(double positionX, double positionY, double positionZ, double quaternionX, double quaternionY, double quaternionZ, double quaternionW);
geometry_msgs::Pose buildRosPose(const Eigen::Vector3d& position, const Eigen::Quaterniond& orientation);

std::string poseToString(tf::Pose pose);


tf::Transform leftHandedToRightHanded(const tf::Transform& leftHandedPose);