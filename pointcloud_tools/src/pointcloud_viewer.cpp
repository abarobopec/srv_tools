/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2009, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: pointcloud_online_viewer.cpp 33238 2010-03-11 00:46:58Z rusu $
 *
 */

// ROS core
#include <signal.h>
#include <ros/ros.h>
#include <std_msgs/String.h>
#include <boost/thread/mutex.hpp>
#include <boost/thread.hpp>
// PCL includes
#include <pcl/point_types.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/features/feature.h>
#include <pcl/common/centroid.h>

using pcl::visualization::PointCloudColorHandlerGenericField;

typedef pcl::PointXYZ             Point;
typedef pcl::PointCloud<Point>    PointCloud;
typedef pcl::PointXYZRGB          PointRGB;
typedef pcl::PointCloud<PointRGB> PointCloudRGB;

// Global data
sensor_msgs::PointCloud2ConstPtr cloud_, cloud_old_;
boost::mutex m;
bool viewer_initialized_;
int counter_;

void cloud_cb(const sensor_msgs::PointCloud2ConstPtr& cloud)
{
  m.lock ();
  printf("\rPointCloud with %d data points (%s), stamp %f, and frame %s.",
      cloud->width * cloud->height, pcl::getFieldsList(*cloud).c_str(), 
      cloud->header.stamp.toSec(), cloud->header.frame_id.c_str());
  cloud_ = cloud;
  m.unlock();
}

void updateVisualization()
{
  PointCloud                    cloud_xyz;
  PointCloudRGB                 cloud_xyz_rgb;
  EIGEN_ALIGN16 Eigen::Matrix3f covariance_matrix;
  Eigen::Vector4f               xyz_centroid;

  ros::WallDuration d(0.01);
  bool rgb = false;
  std::vector<sensor_msgs::PointField> fields;
  
  // Create the visualizer
  pcl::visualization::PCLVisualizer viewer("Point Cloud Viewer");

  // Add a coordinate system to screen
  viewer.addCoordinateSystem(0.1);

  while(true)
  {
    d.sleep();

    // If no cloud received yet, continue
    if(!cloud_ || cloud_->width<=0)
      continue;

    viewer.spinOnce(1);

    if(cloud_old_ == cloud_)
      continue;
    m.lock ();
    
    // Convert to PointCloud<T>
    if(pcl::getFieldIndex(*cloud_, "rgb") != -1)
    {
      rgb = true;
      pcl::fromROSMsg(*cloud_, cloud_xyz_rgb);
    }
    else
    {
      rgb = false;
      pcl::fromROSMsg(*cloud_, cloud_xyz);
      pcl::getFields(cloud_xyz, fields);
    }
    cloud_old_ = cloud_;
    m.unlock();

    // Delete the previous point cloud
    viewer.removePointCloud("cloud");
    
    // If no RGB data present, use a simpler white handler
    if(rgb && pcl::getFieldIndex(cloud_xyz_rgb, "rgb", fields) != -1 && 
      cloud_xyz_rgb.points[0].rgb != 0)
    {
      // Initialize the camera view
      if(!viewer_initialized_)
      {
        computeMeanAndCovarianceMatrix(cloud_xyz_rgb, covariance_matrix, xyz_centroid);
        viewer.initCameraParameters();
        viewer.setCameraPosition(xyz_centroid(0), xyz_centroid(1), xyz_centroid(2)+3.0, 0, -1, 0);
        ROS_INFO_STREAM("[PointCloudViewer:] Point cloud viewer camera initialized in: [" << 
          xyz_centroid(0) << ", " << xyz_centroid(1) << ", " << xyz_centroid(2)+3.0 << "]");
        viewer_initialized_ = true;
      }

      // Show the point cloud
      pcl::visualization::PointCloudColorHandlerRGBField<PointRGB> color_handler(
        cloud_xyz_rgb.makeShared());
      viewer.addPointCloud(cloud_xyz_rgb.makeShared(), color_handler, "cloud");
    }
    else
    {
      // Initialize the camera view
      if(!viewer_initialized_)
      {
        computeMeanAndCovarianceMatrix(cloud_xyz_rgb, covariance_matrix, xyz_centroid);
        viewer.initCameraParameters();
        viewer.setCameraPosition(xyz_centroid(0), xyz_centroid(1), xyz_centroid(2)+3.0, 0, -1, 0);
        ROS_INFO_STREAM("[PointCloudViewer:] Point cloud viewer camera initialized in: [" << 
          xyz_centroid(0) << ", " << xyz_centroid(1) << ", " << xyz_centroid(2)+3.0 << "]");
        viewer_initialized_ = true;
      }

      // Some xyz_rgb point clouds have incorrect rgb field. Detect and convert to xyz.
      if(pcl::getFieldIndex(cloud_xyz_rgb, "rgb", fields) != -1)
      {
        if(cloud_xyz_rgb.points[0].rgb == 0)
        {
          pcl::copyPointCloud(cloud_xyz_rgb, cloud_xyz);
        }
      }
      
      // Show the xyz point cloud
      PointCloudColorHandlerGenericField<Point> color_handler (cloud_xyz.makeShared(), "z");
      if (!color_handler.isCapable ())
      {
        ROS_WARN_STREAM("[PointCloudViewer:] Cannot create curvature color handler!");
        pcl::visualization::PointCloudColorHandlerCustom<Point> color_handler(
        cloud_xyz.makeShared(), 255, 0, 255);
      }

      viewer.addPointCloud(cloud_xyz.makeShared(), color_handler, "cloud");
    }

    counter_++;
  }
}

void sigIntHandler(int sig)
{
  exit(0);
}

/* ---[ */
int main(int argc, char** argv)
{
  ros::init(argc, argv, "pointcloud_viewer", ros::init_options::NoSigintHandler);
  ros::NodeHandle nh;
  viewer_initialized_ = false;
  counter_ = 0;

  // Create a ROS subscriber
  ros::Subscriber sub = nh.subscribe("input", 30, cloud_cb);

  ROS_INFO("Subscribing to %s for PointCloud2 messages...", nh.resolveName ("input").c_str ());

  signal(SIGINT, sigIntHandler);

  boost::thread visualization_thread(&updateVisualization);

  // Spin
  ros::spin();

  // Join, delete, exit
  visualization_thread.join();
  return (0);
}
/* ]--- */
