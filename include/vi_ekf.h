#pragma once

#include "Eigen/Core"
#include "Eigen/Geometry"

#include <set>
#include <map>
#include <functional>

#include "quat.h"
#include "math_helper.h"

namespace vi_ekf
{

class VIEKF;
typedef void (VIEKF::*measurement_function_ptr)(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);

static const Eigen::Vector3d gravity = [] {
  Eigen::Vector3d tmp;
  tmp << 0, 0, 9.80665;
  return tmp;
}();

class VIEKF
{
public:

  enum : int{
    xPOS = 0,
    xVEL = 3,
    xATT = 6,
    xB_A = 10,
    xB_G = 13,
    xMU = 16,
    xZ = 17
  };

  enum : int{
    uA = 0,
    uG = 3,
    uTOTAL = 6
  };

  enum : int {
    dxPOS = 0,
    dxVEL = 3,
    dxATT = 6,
    dxB_A = 9,
    dxB_G = 12,
    dxMU = 15,
    dxZ = 16
  };

  typedef enum {
    ACC,
    ALT,
    ATT,
    POS,
    VEL,
    QZETA,
    FEAT,
    PIXEL_VEL,
    DEPTH,
    INV_DEPTH
  } measurement_type_t;

private:
  Eigen::VectorXd x_;
  Eigen::MatrixXd P_;

  Eigen::Matrix3d P0_feat_;
  Eigen::Matrix<double, dxZ, dxZ> Qx_;
  Eigen::Matrix3d Qx_feat_;
  Eigen::Matrix<double, 6, 6> Qu_;

  int len_features_;
  int next_feature_id_;
  std::set<int> initialized_features_;
  std::map<int, int> global_to_local_feature_id_;
  std::map<measurement_type_t, measurement_function_ptr> measurement_functions_;

  // Matrix Workspace
  Eigen::MatrixXd A_;
  Eigen::MatrixXd G_;
  Eigen::MatrixXd I_big_;
  Eigen::VectorXd dx_;

  bool use_drag_term;
  double prev_t_;

  // Camera Configuration
  Eigen::Vector2d cam_center_;
  Eigen::Matrix<double, 2, 3> cam_F_;
  quat::Quaternion q_b_c_;
  Eigen::Vector3d p_b_c_;

public:

  VIEKF(Eigen::MatrixXd x0, bool multirotor=true);
  void set_camera_to_IMU(const Eigen::Vector3d& translation, const quat::Quaternion& rotation);
  void set_camera_intrinsics(const Eigen::Vector2d& center, const Eigen::Vector2d& focal_len);
  Eigen::VectorXd get_depths();
  Eigen::MatrixXd get_zetas();
  Eigen::MatrixXd get_qzetas();
  Eigen::VectorXd get_zeta(const int i);
  double get_depth(const int i);
  inline int get_len_features() { return len_features_; }

  void boxplus(const Eigen::VectorXd& x, const Eigen::VectorXd& dx, Eigen::VectorXd &out);
  void propagate(Eigen::VectorXd& x, Eigen::MatrixXd& P, const Eigen::Matrix<double, 6, 1> u, const double t);

  Eigen::VectorXd update(Eigen::VectorXd& z, const measurement_type_t meas_type,
                         const Eigen::MatrixXd& R, bool passive=false, const int id = -1);

  void set_imu_bias(const Eigen::Vector3d& b_g, const Eigen::Vector3d& b_a);

  int init_feature(const Eigen::Vector4d& q_zeta, const int id, const double depth=-1.0);
  void clear_feature(const int id);
  void keep_only_features(Eigen::VectorXd features);
  void dynamics(const Eigen::VectorXd& x, const Eigen::MatrixXd& u, Eigen::VectorXd& xdot,
                Eigen::MatrixXd& dfdx, Eigen::MatrixXd& dfdu);

  void h_acc(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);
  void h_alt(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);
  void h_att(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);
  void h_pos(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);
  void h_vel(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);
  void h_qzeta(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);
  void h_feat(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);
  void h_depth(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);
  void h_inv_depth(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);
  void h_pixel_vel(const Eigen::VectorXd& x, Eigen::VectorXd& h, Eigen::VectorXd& H, const int id);



};

}
