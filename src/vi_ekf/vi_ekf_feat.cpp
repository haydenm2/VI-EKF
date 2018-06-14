#include "vi_ekf.h"

namespace vi_ekf 
{

bool VIEKF::init_feature(const pixVector& l, const double depth=NAN)
{
  // If we already have a full set of features, we can't do anything about this new one
  if (len_features_ >= NUM_FEATURES)
    return false;
  
  // Adjust lambdas to be with respect to image center
  Vector2d l_centered = l - cam_center_;
  
  // Calculate Quaternion to Feature
  Vector3d zeta;
  zeta << l_centered(0), l_centered(1)*(cam_F_(1,1)/cam_F_(0,0)), cam_F_(0,0);
  zeta.normalize();
  Vector4d qzeta = Quat::from_two_unit_vectors(e_z, zeta).elements();
  
  // If depth is NAN (default argument)
  double init_depth = depth;
  if (depth != depth)
  {
    init_depth = 2.0 * min_depth_;
  }
  
  // Create a new feature object and add it to the features list
  PatchVector patch;
  cv2Patch(img_, l, patch);
  double quality = calculate_quality(patch);
  feature_t new_feature = {patch, quality, 0u, next_feature_id_ };
  features_.push_back(new_feature);
  next_feature_id_ += 1;
  len_features_ += 1;
  
  //  Initialize the state vector
  int x_max = xZ + 5*len_features_;
  x_.block<4,1>(x_max - 5, 0) = qzeta;
  x_(x_max - 1 ) = 1.0/init_depth;
  
  // Zero out the cross-covariance and reset the uncertainty on this new feature
  int dx_max = dxZ+3*len_features_;
  P_.block(dx_max-3, 0, 3, dx_max-3).setZero();
  P_.block(0, dx_max-3, dx_max-3, 3).setZero();
  P_.block<3,3>(dx_max-3, dx_max-3) = P0_feat_;
  
  NAN_CHECK;
  
  return true;
}


void VIEKF::clear_feature_state(const int id)
{
  int local_feature_id = global_to_local_feature_id(id);
  int xZETA_i = xZ + 5 * local_feature_id;
  int dxZETA_i = dxZ + 3 * local_feature_id;
  features_.erase(features_.begin() + local_feature_id);
  len_features_ -= 1;
  int dx_max = dxZ+3*len_features_;
  int x_max = xZ+5*len_features_;
  
  // Remove the right portions of state and covariance and shift everything to the upper-left corner of the matrix
  if (local_feature_id < len_features_)
  {
    x_.block(xZETA_i, 0, (x_.rows() - (xZETA_i+5)), 1) = x_.bottomRows(x_.rows() - (xZETA_i + 5));
    P_.block(dxZETA_i, 0, (P_.rows() - (dxZETA_i+3)), P_.cols()) = P_.bottomRows(P_.rows() - (dxZETA_i+3));
    P_.block(0, dxZETA_i, P_.rows(), (P_.cols() - (dxZETA_i+3))) = P_.rightCols(P_.cols() - (dxZETA_i+3));
  }
  
  // Clean up the rest of the matrix
  x_.bottomRows(x_.rows() - x_max).setZero();
  P_.rightCols(P_.cols() - dx_max).setZero();
  P_.bottomRows(P_.rows() - dx_max).setZero();
  
  NAN_CHECK;
}

double VIEKF::calculate_quality(const PatchVector &I)
{
  (void)I;
  return 0.0;
}


//void VIEKF::keep_only_features(const vector<int> features)
//{
//  std::vector<int> features_to_remove;
//  int num_overlapping_features = 0;
//  for (int local_id = 0; local_id < current_feature_ids_.size(); local_id++)
//  {
//    // See if we should keep this feature
//    bool keep_feature = false;
//    for (int i = 0; i < features.size(); i++)
//    {
//      if (current_feature_ids_[local_id] == features[i])
//      {
//        keep_feature = true;
//        if (keyframe_reset_)
//        {
//          // See if it overlaps with our keyframe features
//          for (int j = 0; j < keyframe_features_.size(); j++)
//          {
//            if (keyframe_features_[j] == features[i])
//            {
//              num_overlapping_features++;
//              break;
//            }
//          }
//        }
//        break;
//      }
//    }
//    if (!keep_feature)
//    {
//      features_to_remove.push_back(current_feature_ids_[local_id]);
//    }
//  }
//  for (int i = 0; i < features_to_remove.size(); i++)
//  {
//    clear_feature_state(features_to_remove[i]);
//  }
  
//  if (keyframe_reset_ && keyframe_features_.size() > 0 
//      && (double)num_overlapping_features / (double)keyframe_features_.size() < keyframe_overlap_threshold_)
//  {
//    // perform keyframe reset
//    keyframe_reset();
//    // rebuild the list of features for overlap detection
//    keyframe_features_.resize(features.size());
//    for (int i = 0; i < features.size(); i++)
//    {
//      keyframe_features_[i] = features[i];
//    }
//  }
//  else if (keyframe_reset_ && keyframe_features_.size() == 0)
//  {    
//    // build the list of features for overlap detection
//    keyframe_features_.resize(features.size());
//    for (int i = 0; i < features.size(); i++)
//    {
//      keyframe_features_[i] = features[i];
//    }
//  }
  
//  NAN_CHECK;
//}

}
