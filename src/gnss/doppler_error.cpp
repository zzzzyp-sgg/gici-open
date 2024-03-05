/**
* @Function: Doppler residual block for ceres backend
*
* @Author  : Cheng Chi
* @Email   : chichengcn@sjtu.edu.cn
*
* Copyright (C) 2023 by Cheng Chi, All rights reserved.
**/
#include "gici/gnss/doppler_error.h"

#include "gici/gnss/gnss_common.h"
#include "gici/utility/transform.h"
#include "gici/estimate/pose_local_parameterization.h"

namespace gici {

// Construct with measurement and information matrix
template<int... Ns>
DopplerError<Ns ...>::DopplerError(
                       const GnssMeasurement& measurement,
                       const GnssMeasurementIndex index,
                       const GnssErrorParameter& error_parameter)
{
  setMeasurement(measurement);
  satellite_ = measurement_.getSat(index);
  observation_ = measurement_.getObs(index);

  // Check parameter block types
  // Group 1
  if (dims_.kNumParameterBlocks == 3 && 
      dims_.GetDim(0) == 3 && dims_.GetDim(1) == 3 && 
      dims_.GetDim(2) == 1) {
    is_estimate_body_ = false;
    parameter_block_group_ = 1;
  }
  // Group 2
  else if (dims_.kNumParameterBlocks == 4 &&
      dims_.GetDim(0) == 7 && dims_.GetDim(1) == 9 &&
      dims_.GetDim(2) == 3 && dims_.GetDim(3) == 1) {
    is_estimate_body_ = true;
    parameter_block_group_ = 2;
  }
  else {
    LOG(FATAL) << "DopplerError parameter blocks setup invalid!";
  }

  setInformation(error_parameter);
}

// Construct with measurement and information matrix
template<int... Ns>
DopplerError<Ns ...>::DopplerError(
                       const GnssMeasurement& measurement,
                       const GnssMeasurementIndex index,
                       const GnssErrorParameter& error_parameter,
                       const Eigen::Vector3d& angular_velocity)
  : DopplerError(measurement, index, error_parameter)
{
  angular_velocity_ = angular_velocity; // 在ENU融合框架下需要给角速度赋值
}

// Set the information.
template<int... Ns>
void DopplerError<Ns ...>::setInformation(const GnssErrorParameter& error_parameter)
{
  // compute variance
  error_parameter_ = error_parameter;
  double factor = error_parameter_.doppler_error_factor;
  covariance_ = covariance_t(square(factor));
  char system = satellite_.getSystem();
  covariance_ *= square(error_parameter_.system_error_ratio.at(system));

  information_ = covariance_.inverse();
  // perform the Cholesky decomposition on order to obtain the correct error weighting
  Eigen::LLT<information_t> lltOfInformation(information_);
  square_root_information_ = lltOfInformation.matrixL().transpose();
  square_root_information_inverse_ = square_root_information_.inverse();
}

// This evaluates the error term and additionally computes the Jacobians.
template<int... Ns>
bool DopplerError<Ns ...>::Evaluate(double const* const * parameters,
                                 double* residuals, double** jacobians) const
{
  return EvaluateWithMinimalJacobians(parameters, residuals, jacobians, nullptr);
}

// This evaluates the error term and additionally computes
// the Jacobians in the minimal internal representation.
template<int... Ns>
bool DopplerError<Ns ...>::EvaluateWithMinimalJacobians(
    double const* const * parameters, double* residuals, double** jacobians,
    double** jacobians_minimal) const
{
  Eigen::Vector3d t_WR_ECEF, t_WS_W, t_SR_S;
  Eigen::Quaterniond q_WS;
  Eigen::Vector3d v_WR_ECEF, v_WS;
  double clock_frequency;
  
  // Position and clock
  if (!is_estimate_body_) 
  {
    t_WR_ECEF = Eigen::Map<const Eigen::Vector3d>(parameters[0]);
    v_WR_ECEF = Eigen::Map<const Eigen::Vector3d>(parameters[1]);
    clock_frequency = parameters[2][0];
  }
  else 
  {
    // pose in ENU frame
    t_WS_W = Eigen::Map<const Eigen::Vector3d>(&parameters[0][0]);
    q_WS = Eigen::Map<const Eigen::Quaterniond>(&parameters[0][3]);

    // velocity in ENU frame
    v_WS = Eigen::Map<const Eigen::Vector3d>(&parameters[1][0]);

    // relative position
    t_SR_S = Eigen::Map<const Eigen::Vector3d>(parameters[2]);

    // clock
    clock_frequency = parameters[3][0];

    // receiver position
    Eigen::Vector3d t_WR_W = t_WS_W + q_WS * t_SR_S;

    // receiver velocity
    Eigen::Vector3d v_WR = v_WS + skewSymmetric(angular_velocity_) * q_WS * t_SR_S;

    if (!coordinate_) {
      LOG(FATAL) << "Coordinate not set!";
    }
    if (!coordinate_->isZeroSetted()) {
      LOG(FATAL) << "Coordinate zero not set!";
    }
    t_WR_ECEF = coordinate_->convert(t_WR_W, GeoType::ENU, GeoType::ECEF);
    v_WR_ECEF = coordinate_->rotate(v_WR, GeoType::ENU, GeoType::ECEF);
  }

  double timestamp = measurement_.timestamp;
  double rho = gnss_common::satelliteToReceiverDistance(
    satellite_.sat_position, t_WR_ECEF);
  double elevation = gnss_common::satelliteElevation(
    satellite_.sat_position, t_WR_ECEF);
  double azimuth = gnss_common::satelliteAzimuth(
    satellite_.sat_position, t_WR_ECEF);

  // Get estimate derivated measurement
  Eigen::Vector3d e = (satellite_.sat_position - t_WR_ECEF) / rho;
  Eigen::Vector3d v_sat = satellite_.sat_velocity;
  Eigen::Vector3d p_sat = satellite_.sat_position;
  Eigen::Vector3d vs = v_sat - v_WR_ECEF;
  // range rate with earth rotation correction
  double range_rate = vs.dot(e) + OMGE / CLIGHT *
      (v_sat(1) * t_WR_ECEF(0) + p_sat(1) * v_WR_ECEF(0) -
       v_sat(0) * t_WR_ECEF(1) - p_sat(0) * v_WR_ECEF(1));
  double doppler_estimate = 
    range_rate + clock_frequency - satellite_.sat_frequency;

  // Compute error
  double doppler = observation_.doppler;
  Eigen::Matrix<double, 1, 1> error = 
    Eigen::Matrix<double, 1, 1>(doppler - doppler_estimate);

  // weigh it
  Eigen::Map<Eigen::Matrix<double, 1, 1> > weighted_error(residuals);
  weighted_error = square_root_information_ * error;

  // compute Jacobian
  if (jacobians != nullptr)
  {
    // Receiver position in ECEF
    Eigen::Matrix<double, 1, 3> J_t_ECEF = Eigen::Matrix<double, 1, 3>::Zero();

    // Receiver velocity in ECEF
    Eigen::Matrix<double, 1, 3> J_v_ECEF = 
      -((t_WR_ECEF - satellite_.sat_position) / rho).transpose();

    // Poses and velocities in ENU
    Eigen::Matrix<double, 1, 6> J_T_WS;
    Eigen::Matrix<double, 1, 9> J_speed_and_bias;
    Eigen::Matrix<double, 1, 3> J_t_SR_S;
    if (is_estimate_body_) {
      // Body position in ENU
      Eigen::Matrix<double, 1, 3> J_t_W = Eigen::Matrix<double, 1, 3>::Zero();

      // Body velocity in ENU
      Eigen::Matrix3d R_ECEF_ENU = coordinate_->rotationMatrix(
        GeoType::ENU, GeoType::ECEF);
      Eigen::Matrix<double, 1, 3> J_v_W = J_v_ECEF * R_ECEF_ENU;

      // Body rotation in ENU
      Eigen::Matrix<double, 1, 3> J_q_WS = J_v_W * 
        skewSymmetric(angular_velocity_) * 
        -skewSymmetric(q_WS.toRotationMatrix() * t_SR_S);

      // Body pose in ENU
      J_T_WS.setZero();
      J_T_WS.topLeftCorner(1, 3) = J_t_W;
      J_T_WS.topRightCorner(1, 3) = J_q_WS;

      // Speed and bias
      J_speed_and_bias.setZero();
      J_speed_and_bias.topLeftCorner(1, 3) = J_v_W;

      // Relative position 
      J_t_SR_S = J_v_W * skewSymmetric(angular_velocity_) * 
                 q_WS.toRotationMatrix();
    }

    // Clock frequency
    Eigen::Matrix<double, 1, 1> J_freq = -Eigen::MatrixXd::Identity(1, 1);

    // Group 1
    if (parameter_block_group_ == 1) 
    {
      // Position
      if (jacobians[0] != nullptr) {
        Eigen::Map<Eigen::Matrix<double, 1, 3, Eigen::RowMajor>> J0(jacobians[0]);
        J0 = square_root_information_ * J_t_ECEF;
        
        if (jacobians_minimal != nullptr && jacobians_minimal[0] != nullptr) {
          Eigen::Map<Eigen::Matrix<double, 1, 3, Eigen::RowMajor> >
              J0_minimal_mapped(jacobians_minimal[0]);
          J0_minimal_mapped = J0;
        }
      }
      // Velocity
      if (jacobians[1] != nullptr) {
        Eigen::Map<Eigen::Matrix<double, 1, 3, Eigen::RowMajor>> J1(jacobians[1]);
        J1 = square_root_information_ * J_v_ECEF;

        if (jacobians_minimal != nullptr && jacobians_minimal[1] != nullptr) {
          Eigen::Map<Eigen::Matrix<double, 1, 3, Eigen::RowMajor> >
              J1_minimal_mapped(jacobians_minimal[1]);
          J1_minimal_mapped = J1;
        }
      }
      // Frequency
      if (jacobians[2] != nullptr) {
        Eigen::Map<Eigen::Matrix<double, 1, 1, Eigen::RowMajor>> J2(jacobians[2]);
        J2 = square_root_information_ * J_freq;

        if (jacobians_minimal != nullptr && jacobians_minimal[2] != nullptr) {
          Eigen::Map<Eigen::Matrix<double, 1, 1, Eigen::RowMajor> >
              J2_minimal_mapped(jacobians_minimal[2]);
          J2_minimal_mapped = J2;
        }
      }
    }
    // Group 2
    if (parameter_block_group_ == 2)
    {
      // Pose
      if (jacobians[0] != nullptr) {
        Eigen::Map<Eigen::Matrix<double, 1, 7, Eigen::RowMajor>> J0(jacobians[0]);
        Eigen::Matrix<double, 1, 6, Eigen::RowMajor> J0_minimal;
        J0_minimal = square_root_information_ * J_T_WS;

        // pseudo inverse of the local parametrization Jacobian:
        Eigen::Matrix<double, 6, 7, Eigen::RowMajor> J_lift;
        PoseLocalParameterization::liftJacobian(parameters[0], J_lift.data());

        J0 = J0_minimal * J_lift;

        if (jacobians_minimal != nullptr && jacobians_minimal[0] != nullptr) {
          Eigen::Map<Eigen::Matrix<double, 1, 6, Eigen::RowMajor> >
              J0_minimal_mapped(jacobians_minimal[0]);
          J0_minimal_mapped = J0_minimal;
        }
      }
      // Speed and biases
      if (jacobians[1] != nullptr) {
        Eigen::Map<Eigen::Matrix<double, 1, 9, Eigen::RowMajor>> J1(jacobians[1]);
        J1 = square_root_information_ * J_speed_and_bias;

        if (jacobians_minimal != nullptr && jacobians_minimal[1] != nullptr) {
          Eigen::Map<Eigen::Matrix<double, 1, 9, Eigen::RowMajor> >
              J1_minimal_mapped(jacobians_minimal[1]);
          J1_minimal_mapped = J1;
        }
      }
      // Relative position
      if (jacobians[2] != nullptr) {
        Eigen::Map<Eigen::Matrix<double, 1, 3, Eigen::RowMajor>> J2(jacobians[2]);
        J2 = square_root_information_ * J_t_SR_S;

        if (jacobians_minimal != nullptr && jacobians_minimal[2] != nullptr) {
          Eigen::Map<Eigen::Matrix<double, 1, 3, Eigen::RowMajor> >
              J2_minimal_mapped(jacobians_minimal[2]);
          J2_minimal_mapped = J2;
        }
      }
      // Frequency
      if (jacobians[3] != nullptr) {
        Eigen::Map<Eigen::Matrix<double, 1, 1, Eigen::RowMajor>> J3(jacobians[3]);
        J3 = square_root_information_ * J_freq;

        if (jacobians_minimal != nullptr && jacobians_minimal[3] != nullptr) {
          Eigen::Map<Eigen::Matrix<double, 1, 1, Eigen::RowMajor> >
              J3_minimal_mapped(jacobians_minimal[3]);
          J3_minimal_mapped = J3;
        }
      }
    }
  }

  return true;
}

}  // namespace gici
