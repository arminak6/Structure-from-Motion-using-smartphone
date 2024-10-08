#include "features_matcher.h"


#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/flann.hpp>
#include "opencv2/highgui.hpp"
#include "opencv2/features2d.hpp"
#include <iostream>
#include <map>

using namespace cv;
using namespace std;

FeatureMatcher::FeatureMatcher(cv::Mat intrinsics_matrix, cv::Mat dist_coeffs, double focal_scale)
{
  intrinsics_matrix_ = intrinsics_matrix.clone();
  dist_coeffs_ = dist_coeffs.clone();
  new_intrinsics_matrix_ = intrinsics_matrix.clone();
  new_intrinsics_matrix_.at<double>(0,0) *= focal_scale;
  new_intrinsics_matrix_.at<double>(1,1) *= focal_scale;
}

cv::Mat FeatureMatcher::readUndistortedImage(const std::string& filename )
{
  cv::Mat img = cv::imread(filename), und_img, dbg_img;
  cv::undistort	(	img, und_img, intrinsics_matrix_, dist_coeffs_, new_intrinsics_matrix_ );

  return und_img;
}

void FeatureMatcher::extractFeatures()
{
  features_.resize(images_names_.size());
  descriptors_.resize(images_names_.size());
  feats_colors_.resize(images_names_.size());

  for( int i = 0; i < images_names_.size(); i++  )
  {
    std::cout<<"Computing descriptors for image "<<i<<std::endl;
    cv::Mat img = readUndistortedImage(images_names_[i]);
    //////////////////////////// Code to be completed (1/7) /////////////////////////////////
    // Extract salient points + descriptors from i-th image, and store them into
    // features_[i] and descriptors_[i] vector, respectively
    // Extract also the color (i.e., the cv::Vec3b information) of each feature, and store
    // it into feats_colors_[i] vector
    /////////////////////////////////////////////////////////////////////////////////////////

    cv::Ptr<cv::Feature2D> orb = cv::ORB::create(10000, 1.2f, 8, 31, 0, 2, cv::ORB::FAST_SCORE, 31, 20);

    // Detect keypoints and compute descriptors
    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
    orb->detectAndCompute(img, cv::noArray(), keypoints, descriptors);

    // Store keypoints and descriptors
    features_[i] = keypoints;
    descriptors_[i] = descriptors;

    // Extract colors of keypoints
    std::vector<cv::Vec3b> feature_colors(keypoints.size());
    for (int j = 0; j < keypoints.size(); j++)
    {
        feature_colors[j] = img.at<cv::Vec3b>(keypoints[j].pt.y, keypoints[j].pt.x);
    }
    feats_colors_[i] = feature_colors;
    /////////////////////////////////////////////////////////////////////////////////////////
  }
}

void FeatureMatcher::exhaustiveMatching()
{
  std::vector<cv::DMatch> matches, inlier_matches;
  
  for( int i = 0; i < images_names_.size() - 1; i++ )
  {
    for( int j = i + 1; j < images_names_.size(); j++ )
    {
      std::cout<<"Matching image "<<i<<" with image "<<j<<std::endl;

      //////////////////////////// Code to be completed (2/7) /////////////////////////////////
      // Match descriptors between image i and image j, and perform geometric validation,
      // possibly discarding the outliers (remember that features have been extracted
      // from undistorted images that now has new_intrinsics_matrix_ as K matrix and
      // no distortions)
      // As geometric models, use both the Essential matrix and the Homograph matrix,
      // both by setting new_intrinsics_matrix_ as K matrix.
      // As threshold in the functions to estimate both models, you may use 1.0 or similar.
      // Store inlier matches into the inlier_matches vector
      // Do not set matches between two images if the amount of inliers matches
      // (i.e., geomatrically verified matches) is small (say <= 5 matches)
      // In case of success, set the matches with the function:
      // setMatches( i, j, inlier_matches);
      /////////////////////////////////////////////////////////////////////////////////////////

      // BFMatcher with Hamming distances
      Ptr<DescriptorMatcher> matcher = BFMatcher::create(NORM_HAMMING, true);
      
      // Simple Match descriptors
      matcher->match(descriptors_[i], descriptors_[j], matches);
      // OR use KNN match
      // matcher->knnMatch(descriptors_[i], descriptors_[j], matches, 2);


      // Sort them in the order of their distance.
      sort(matches.begin(), matches.end(), [](const DMatch& a, const DMatch& b) { return a.distance < b.distance; });
      // Extract corresponding keypoints from matched descriptors
      vector<Point2f> points1, points2;
      for (int k = 0; k < matches.size(); k++)
      {
        points1.push_back(features_[i][matches[k].queryIdx].pt);
        points2.push_back(features_[j][matches[k].trainIdx].pt);
      }


      // Compute essential matrix and inliers
      Mat mask_E,mask_H;
      Mat E = findEssentialMat(points1, points2, new_intrinsics_matrix_, RANSAC, 0.999, 1.0, mask_E);
      int num_inliers_E = cv::countNonZero(mask_E);


      Mat H = findHomography(points1, points2, RANSAC, 1.0, mask_H);
      int num_inliers_H = cv::countNonZero(mask_H);

      inlier_matches.clear();
      for (int k = 0; k < matches.size(); k++)
      {
          // Check if the match is an inlier according to the Essential Matrix
          if (num_inliers_E > num_inliers_H && mask_E.at<uchar>(k))
          {
              inlier_matches.push_back(matches[k]);
          }
          // Check if the match is an inlier according to the Homography Matrix
          else if (num_inliers_E <= num_inliers_H && mask_H.at<uchar>(k))
          {
              inlier_matches.push_back(matches[k]);
          }
      }

      // Set matches if there are enough inliers
      if (inlier_matches.size() > 5)
      {
          setMatches(i, j, inlier_matches);
      }
      else
      {
          std::cout << "Not enough inliers to set matches between image " << i << " and image " << j << std::endl;
      }

      
      /////////////////////////////////////////////////////////////////////////////////////////

    }
  }
}

void FeatureMatcher::writeToFile ( const std::string& filename, bool normalize_points ) const
{
  FILE* fptr = fopen(filename.c_str(), "w");

  if (fptr == NULL) {
    std::cerr << "Error: unable to open file " << filename;
    return;
  };

  fprintf(fptr, "%d %d %d\n", num_poses_, num_points_, num_observations_);

  double *tmp_observations;
  cv::Mat dst_pts;
  if(normalize_points)
  {
    cv::Mat src_obs( num_observations_,1, cv::traits::Type<cv::Vec2d>::value,
                     const_cast<double *>(observations_.data()));
    cv::undistortPoints(src_obs, dst_pts, new_intrinsics_matrix_, cv::Mat());
    tmp_observations = reinterpret_cast<double *>(dst_pts.data);
  }
  else
  {
    tmp_observations = const_cast<double *>(observations_.data());
  }

  for (int i = 0; i < num_observations_; ++i)
  {
    fprintf(fptr, "%d %d", pose_index_[i], point_index_[i]);
    for (int j = 0; j < 2; ++j) {
      fprintf(fptr, " %g", tmp_observations[2 * i + j]);
    }
    fprintf(fptr, "\n");
  }

  if( colors_.size() == 3*num_points_ )
  {
    for (int i = 0; i < num_points_; ++i)
      fprintf(fptr, "%d %d %d\n", colors_[i*3], colors_[i*3 + 1], colors_[i*3 + 2]);
  }

  fclose(fptr);
}

void FeatureMatcher::testMatches( double scale )
{
  // For each pose, prepare a map that reports the pairs [point index, observation index]
  std::vector< std::map<int,int> > cam_observation( num_poses_ );
  for( int i_obs = 0; i_obs < num_observations_; i_obs++ )
  {
    int i_cam = pose_index_[i_obs], i_pt = point_index_[i_obs];
    cam_observation[i_cam][i_pt] = i_obs;
  }

  for( int r = 0; r < num_poses_; r++ )
  {
    for (int c = r + 1; c < num_poses_; c++)
    {
      int num_mathces = 0;
      std::vector<cv::DMatch> matches;
      std::vector<cv::KeyPoint> features0, features1;
      for (auto const &co_iter: cam_observation[r])
      {
        if (cam_observation[c].find(co_iter.first) != cam_observation[c].end())
        {
          features0.emplace_back(observations_[2*co_iter.second],observations_[2*co_iter.second + 1], 0.0);
          features1.emplace_back(observations_[2*cam_observation[c][co_iter.first]],observations_[2*cam_observation[c][co_iter.first] + 1], 0.0);
          matches.emplace_back(num_mathces,num_mathces, 0);
          num_mathces++;
        }
      }
      cv::Mat img0 = readUndistortedImage(images_names_[r]),
          img1 = readUndistortedImage(images_names_[c]),
          dbg_img;

      cv::drawMatches(img0, features0, img1, features1, matches, dbg_img);
      cv::resize(dbg_img, dbg_img, cv::Size(), scale, scale);
      cv::imshow("", dbg_img);
      if (cv::waitKey() == 27)
        return;
    }
  }
}

void FeatureMatcher::setMatches( int pos0_id, int pos1_id, const std::vector<cv::DMatch> &matches )
{

  const auto &features0 = features_[pos0_id];
  const auto &features1 = features_[pos1_id];

  auto pos_iter0 = pose_id_map_.find(pos0_id),
      pos_iter1 = pose_id_map_.find(pos1_id);

  // Already included position?
  if( pos_iter0 == pose_id_map_.end() )
  {
    pose_id_map_[pos0_id] = num_poses_;
    pos0_id = num_poses_++;
  }
  else
    pos0_id = pose_id_map_[pos0_id];

  // Already included position?
  if( pos_iter1 == pose_id_map_.end() )
  {
    pose_id_map_[pos1_id] = num_poses_;
    pos1_id = num_poses_++;
  }
  else
    pos1_id = pose_id_map_[pos1_id];

  for( auto &match:matches)
  {

    // Already included observations?
    uint64_t obs_id0 = poseFeatPairID(pos0_id, match.queryIdx ),
        obs_id1 = poseFeatPairID(pos1_id, match.trainIdx );
    auto pt_iter0 = point_id_map_.find(obs_id0),
        pt_iter1 = point_id_map_.find(obs_id1);
    // New point
    if( pt_iter0 == point_id_map_.end() && pt_iter1 == point_id_map_.end() )
    {
      int pt_idx = num_points_++;
      point_id_map_[obs_id0] = point_id_map_[obs_id1] = pt_idx;

      point_index_.push_back(pt_idx);
      point_index_.push_back(pt_idx);
      pose_index_.push_back(pos0_id);
      pose_index_.push_back(pos1_id);
      observations_.push_back(features0[match.queryIdx].pt.x);
      observations_.push_back(features0[match.queryIdx].pt.y);
      observations_.push_back(features1[match.trainIdx].pt.x);
      observations_.push_back(features1[match.trainIdx].pt.y);

      // Average color between two corresponding features (suboptimal since we shouls also consider
      // the other observations of the same point in the other images)
      cv::Vec3f color = (cv::Vec3f(feats_colors_[pos0_id][match.queryIdx]) +
                        cv::Vec3f(feats_colors_[pos1_id][match.trainIdx]))/2;

      colors_.push_back(cvRound(color[2]));
      colors_.push_back(cvRound(color[1]));
      colors_.push_back(cvRound(color[0]));

      num_observations_++;
      num_observations_++;
    }
      // New observation
    else if( pt_iter0 == point_id_map_.end() )
    {
      int pt_idx = point_id_map_[obs_id1];
      point_id_map_[obs_id0] = pt_idx;

      point_index_.push_back(pt_idx);
      pose_index_.push_back(pos0_id);
      observations_.push_back(features0[match.queryIdx].pt.x);
      observations_.push_back(features0[match.queryIdx].pt.y);
      num_observations_++;
    }
    else if( pt_iter1 == point_id_map_.end() )
    {
      int pt_idx = point_id_map_[obs_id0];
      point_id_map_[obs_id1] = pt_idx;

      point_index_.push_back(pt_idx);
      pose_index_.push_back(pos1_id);
      observations_.push_back(features1[match.trainIdx].pt.x);
      observations_.push_back(features1[match.trainIdx].pt.y);
      num_observations_++;
    }
//    else if( pt_iter0->second != pt_iter1->second )
//    {
//      std::cerr<<"Shared observations does not share 3D point!"<<std::endl;
//    }
  }
}
void FeatureMatcher::reset()
{
  point_index_.clear();
  pose_index_.clear();
  observations_.clear();
  colors_.clear();

  num_poses_ = num_points_ = num_observations_ = 0;
}