#include "vi_ekf.h"

namespace vi_ekf 
{

/**
 * @brief VIEKF::init_feature
 * Initializes a new feature in the state.  qzeta is set using the inverse projection
 * from the pixel location.  Optionally supplied depth will initialize the depth, otherwise
 * depth will be initialized to the default (2*min_depth)
 * @param z - pixel location
 * @param depth - optional (default = NAN)
 * @return false if unable to add feature, true if successfully added
 */
bool VIEKF::init_feature(const Vector2d& z, const double depth=NAN)
{
  // If we already have a full set of features, we can't do anything about this new one
  if (len_features_ >= NUM_FEATURES)
    return false;
  
  // Adjust lambdas to be with respect to image center
  Vector2d l_centered = z - cam_center_;
  
  // Calculate Quaternion to Feature
  Vector3d zeta;
  zeta << l_centered(0), l_centered(1)*(cam_F_(0,0)/cam_F_(1,1)), cam_F_(0,0);
  zeta.normalize();
  Vector4d qzeta = Quat::from_two_unit_vectors(e_z, zeta).elements();
  
  // If depth is NAN (default argument)
  double init_depth = depth;
  if (depth != depth)
  {
    init_depth = 2.0 * min_depth_;
  }
  
  // Create a new feature object and add it to the features list
  feature_t new_feature;
  new_feature.pix = z.cast<float>();
  multiLvlPatch(z.cast<float>(), new_feature.PatchIntensity);
  new_feature.quality = calculate_quality(z.cast<float>());
  new_feature.frames = 0u;
  new_feature.global_id = next_feature_id_;
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

/**
 * @brief VIEKF::clear_feature_state
 * Clears a feature by global id.  removes the associated rows out of the state vector
 * as well as rows and columns from the covariance matrix
 * @param id - global id of feature to remove
 */
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


/**
 * @brief VIEKF::calculate_quality
 * calculates the quality of the given patch
 * @param eta - subpixel center of multilevel patch
 * @return quality level
 */
double VIEKF::calculate_quality(const pixVector &eta)
{
  // Quality is based on the Hessian's eigenvalues. Good points have
  // two large eigenvalues while edges have one large eigenvalue.
  // Since points provide more information, they are preferred. The 
  // 2-norm forces edges to be really good to be picked over good points.

  // Compute multi-level patch gradient by central differencing
  multiPatchJacMatrix J;
  for (int i = 0; i < 2; i++)
  {
    multiLvlPatch(eta + I_2x2f.col(i), Ip_);
    multiLvlPatch(eta - I_2x2f.col(i), Im_);
    J.col(i) = (Ip_ - Im_)/2.0;
  }

  // Output 2-norm of the Hessian
  // Eigen's norm computes the square root of the sum of absolute values 
  // of elements which is greater than or equal to the maximum singular value
  return (J.transpose()*J).norm();
}

/**
 * @brief VIEKF::image_update
 * Main update function from an image - this performs the iterative
 * update in the image and manages features
 * @param img - new image
 * @param t - current time in seconds
 */
void VIEKF::image_update(const Mat& img, const Matrix2d& R, const double t)
{
  (void)t;
  
  set_image(img);
  for (int i = 0; i < len_features_; i++)
  {
    if (iterated_feature_update(i, R) != FEATURE_TRACKED)
    {
      clear_feature_state(features_[i].global_id);
    }
  }
  manage_features();
}

/**
 * @brief VIEKF::iterated_feature_update
 * performs an iterated feature update of local feature id
 * @param id - local feature id
 * @return FEATURE_TRACKED - if the feature was tracked
 *         FEATURE_LOST - if the feature was lost
 */
VIEKF::update_return_code_t VIEKF::iterated_feature_update(const int id, const Matrix2d& R)
{
  // if id not in current_ids
  if (id > len_features_)
    return FEATURE_LOST;
  
  // sample pixels
  pix_.clear();
  pix_copy_.clear();
  int x_id = xZ + 5*id;
  int dx_id = dxZ + 3*id;
  
  Vector2f eta0;
  Matrix2f cov, dpidqz;
  Quat qzhat(x_.block<4,1>(x_id, 0));  // Bearing Quaternion
  proj(qzhat, eta0, dpidqz, false); // Extract the feature location (don't calculate jacobian on this one)
  cov = P_.block<2,2>(dx_id, dx_id).cast<float>(); // Covariance
  sample_pixels(qzhat, cov, pix_);
  
  // if pixel not in mask
  if (mask_.at<uint8_t>(eta0(0,0), eta0(1,0)) == 0)
    return FEATURE_LOST;
  
  multiPatchJacMatrix J;
  multiPatchVectorf e;
  
  // reset the relevant parts of the workspace
  H_.setZero();
  K_.setZero();  
  
  // For each patch we grabbed
  for (int i = 0; i < pix_.size(); i++)
  { 
    pix_copy_.push_back(pix_[i]);
    xs_.push_back(x_);
    int iter = 0;
    bool done = false;
    double current_err = INFINITY;
    double prev_err;
    double min_error = INFINITY;
    int min_idx = -1;
    double eps = INFINITY;
    
    
    // perform an iterated update
    do
    {
      // store error from previous iteration
      prev_err = current_err;

      // refresh the pixel location and associated jacobian
      Quat qzhati(xs_[i].block<4,1>(x_id, 0));
      proj(qzhati, pix_[i], dpidqz, true);
      
      // make sure we haven't made a huge jump
      if ((pix_[i] - pix_copy_[i]).norm() > PATCH_SIZE)
        break;
      
      // make sure we haven't gone off the screen
      if ((!inImage(pix_[i])))
        break;
      
      // Calculate the intensity error and its jacobian
      patch_error(pix_[i], features_[id].PatchIntensity, e, J);
      
      // Do a QR decomposition of the error jacobian
      qrsolver_.compute(J);
      multiPatchJacMatrix Q1 = qrsolver_.householderQ() * multiPatchJacMatrix::Identity();
      Matrix2f R1 = qrsolver_.matrixQR().topRows(2);
      Vector2f r = Q1.transpose() * e;
      H_.block<2,2>(dx_id, 0) = (R1 * dpidqz).cast<double>();
      
      double mahal = r.transpose() * R.inverse().cast<float>() * r;
      
      if (mahal > 9.0)
        break;
      
      // Perform an update step
      K_.leftCols(2) = P_ * H_.topRows(2).transpose() * (R + H_.topRows(2)*P_ * H_.topRows(2).transpose()).inverse();
      
        // NaNs in matrix, so don't update
      if (!NO_NANS(K_) && !NO_NANS(H_))
        break;
      
      boxplus(xs_[i], K_.leftCols(2) * r.cast<double>(), xp_);  
      
      // convergence calculation
      current_err = (xp_ - xs_[i]).norm();
      if (current_err < 1e-2)
      {
        done = true;
        break;
      }
      
      iter++;
    } 
    while (iter < 25 && std::abs(1.0 - current_err/prev_err) > 0.05);
    
    // If we converged, save the index
    if (done)
    {
      // Save off the best match (in terms of intensity)
      min_error = current_err;
      min_idx = i;      
      
      // We tracked the feature, officially update the state and covariance    
      if (partial_update_)
      {
        boxminus(xs_[i], x_, dx_);
        boxplus(x_, lambda_.asDiagonal() * dx_, xp_);
        x_ = xp_; 
        P_ -= (Lambda_).cwiseProduct(K_.leftCols(2) * H_.topRows(2)*P_);
      }
      else
      {
        x_ = xs_[i];
        P_ = (I_big_ - K_.leftCols(2) * H_.topRows(2))*P_;
      }
      features_[id].pix = pix_[i];
      NAN_CHECK;
      return FEATURE_TRACKED;
    }
  }    
  return FEATURE_LOST;
}

/**
 * @brief VIEKF::sample_pixels
 * Takes a given bearing quaternion and associated covariance and samples pixels
 * to start an iterated EKF update step with.
 * @param qz Bearing Quaternion
 * @param cov 2D covariance block of qz
 * @param eta vector of pixel locations (length dependent on size of cov)
 */
void VIEKF::sample_pixels(const Quat& qz, const Matrix2f& cov, std::vector<Vector2f>& eta)
{  
  Vector2f eta0;
  Matrix2f eta_jac;
  proj(qz, eta0, eta_jac, true);
  
  eigensolver_.computeDirect(eta_jac * cov * eta_jac.transpose());
  Vector2f e = eigensolver_.eigenvalues();
  Matrix2f v = eigensolver_.eigenvectors();
  double a = 3.0*std::sqrt(std::abs(e(0,0)));
  double b = 3.0*std::sqrt(std::abs(e(1,0)));
  
  /// TODO: Spiral out from center, instead of along axis to increase search efficiency later
  Vector2f p, pt;
  for (float y = 0; y < b; y += PATCH_SIZE * 1.0)
  {
    p(1,0) = y;
    float xmax = (a/b) * std::sqrt(b*b - y*y);
    for (float x = 0; x < xmax; x += PATCH_SIZE * 1.0)
    {
      p(0,0) = x;
      pt = (v * p + eta0);
      if (inImage(pt))
        eta.push_back(pt);
      if (x != 0)
      {
        p(0, 0) *= -1;
        pt = (v * p + eta0);
        if (inImage(pt))
          eta.push_back(pt);
      }
      if (y != 0)
      {
        p(1, 0) *= -1;
        pt = (v * p + eta0);
        if (inImage(pt))
          eta.push_back(pt);
        if (x != 0)
        {
          p(0, 0) *= -1;
          pt = (v * p + eta0);
          if (inImage(pt))
            eta.push_back(pt);
        }
      }
    }
  }
  
}

/**
 * @brief VIEKF::manage_features
 * Tosses features if features have gotten too close together, finds
 * new features if we need some new ones, Re-extracts patches if
 * we have observed the same patch through several frames
 */
void VIEKF::manage_features()
{
  // Check for features getting too close together (camera moving
  // away from scene), if so keep the one with better quality
  auto it1 = features_.begin();
  while (it1 != features_.end())
  {
    bool hold_it1 = false;
    auto it2 = features_.begin();
    while (it2 != features_.end())
    {
      if (it1 == it2) // Don't compare feature with itself
      {
        ++it2;
      }
      else
      {
        if (((*it1).pix - (*it2).pix).norm() < feature_min_radius_)
        {
          if ((*it1).quality < (*it2).quality)
          {
            clear_feature_state((*it1).global_id);
            hold_it1 = true;
            break;
          }
          else
          {
            clear_feature_state((*it2).global_id);
          }
        }
        else
        {
          ++it2;
        }
      }
    }
    if (!hold_it1)
      ++it1;
  }

  // Update properties of current features
  for (auto& f : features_)
  {
    ++f.frames;
    if (f.frames % patch_refresh_ == 0)
    {
      multiLvlPatch(f.pix, f.PatchIntensity);
      f.quality = calculate_quality(f.pix);
    }
  }
  
  // Extract new features if needed
  int num_new_features = NUM_FEATURES - len_features_;
  if (num_new_features > 0)
  {
    // Collect a set of keypoints
    keypoints_.clear();
    detector_->detect(img_[0], keypoints_, mask_);
    if (keypoints_.empty())
    {
      std::cout << "*** Feature detector was unable to find any features. ***\n";
    }
    else
    {
      // Keep best points with good separation from others
      choose_keypoints(keypoints_, good_keypoints_, img_[0].cols, img_[0].rows, num_new_features);

      // Convert keypoints to point2f
      good_features_.clear();
      cv::KeyPoint::convert(good_keypoints_, good_features_);

      // Initialize new feature states
      for (auto& gf : good_features_)
      {
        vec2d_(0) = gf.x;
        vec2d_(1) = gf.y;
        init_feature(vec2d_, NAN);
      }
    }
  }
}

/**
 * @brief VIEKF::patch_error
 * Calculates the error and jacobian between the multilevel patch at the current image
 * and etahat.
 * @param etahat - current estimated pixel location
 * @param I0 - original intensities of the patch 
 * @param e - intensity error
 * @param J - de/detahat
 */
void VIEKF::patch_error(const pixVector &etahat, const multiPatchVectorf &I0, multiPatchVectorf &e, multiPatchJacMatrix &J)
{
  multiLvlPatch(etahat, Ip_);
  
  e = Ip_ - I0;  
  
  // Perform central differencing
  for (int i = 0; i < 2; i++)
  {
    multiLvlPatch(etahat + I_2x2f.col(i), Ip_);
    multiLvlPatch(etahat - I_2x2f.col(i), Im_);
    J.col(i) = ((Ip_ - I0) - (Im_ - I0))/2.0;
  }
}

/**
 * @brief VIEKF::choose_keypoints
 * Chooses the best keypoints at desired separation.
 * @param keypoints - current set of keypoints
 * @param good_keypoints - output of best keypoints
 * @param image_width - width of image keypoints detected from
 * @param image_height - height of image keypoints detected from
 * @param num_new_points - number of good keypoints to keep
 */
void VIEKF::choose_keypoints(std::vector<cv::KeyPoint> &keypoints, std::vector<cv::KeyPoint> &good_keypoints, const int &image_width, const int &image_height, const int &num_new_points)
{
  // declarations
  good_keypoints.clear();
  size_t i, j, num_keypoints = keypoints.size();

  // sort keypoints in descending order of response
  std::sort(keypoints.begin(), keypoints.end(), keypoint_sort_key());

  // don't bother checking distance unless minimum distance is significant
  if (feature_detect_radius_ >= 1)
  {
    // partition image into a grid
    const int cell_size = (int)feature_detect_radius_;
    const int grid_width = (image_width + cell_size - 1) / cell_size;
    const int grid_height = (image_height + cell_size - 1) / cell_size;
    std::vector<std::vector<cv::Point2f> > grid(grid_width*grid_height);

    // compute squared minimum distance for comparison later
    static double md2 = feature_detect_radius_ * feature_detect_radius_;

    // loop through keypoints, keeping better ones first
    for(i = 0; i < num_keypoints; i++)
    {
      // get feature point components
      float x = keypoints[i].pt.x;
      float y = keypoints[i].pt.y;

      // assume good point unless proven otherwise
      bool good = true;

      // determine points position in the grid
      int x_cell = (int)x / cell_size;
      int y_cell = (int)y / cell_size;

      // define boundary cells
      int x1 = x_cell - 1;
      int y1 = y_cell - 1;
      int x2 = x_cell + 1;
      int y2 = y_cell + 1;

      // prevent overstepping at boundaries
      x1 = std::max(0, x1);
      y1 = std::max(0, y1);
      x2 = std::min(grid_width-1, x2);
      y2 = std::min(grid_height-1, y2);

      // check distance from points in own and surrounding grid cells
      for(int yy = y1; yy <= y2; yy++)
      {
        for(int xx = x1; xx <= x2; xx++)
        {
          // pull out points from current grid cell
          std::vector<cv::Point2f> m = grid[yy*grid_width + xx];

          // check distance from points in current cell
          if(!m.empty())
          {
            for(j = 0; j < m.size(); j++)
            {
              float dx = x - m[j].x;
              float dy = y - m[j].y;

              // drop keypoint if it's too close to another one
              if(dx*dx + dy*dy < md2)
              {
                good = false;
                goto break_out;
              }
            }
          }
        }
      }

      break_out:

      // if keypoint is not too close to another one, add it to the grid and save it
      if (good)
      {
        grid[y_cell*grid_width + x_cell].emplace_back(cv::Point2f(x, y));
        good_keypoints.push_back(keypoints[i]);
      }
    }
  }
  else
  {
    // since minimum distance wasn't significant, keep all keypoints
    for(i = 0; i < num_keypoints; i++)
      good_keypoints.push_back(keypoints[i]);
  }

  // keep desired number of good keypoints
  while (good_keypoints.size() > num_new_points)
    good_keypoints.pop_back();
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
