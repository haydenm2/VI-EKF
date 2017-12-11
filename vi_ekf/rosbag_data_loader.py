import csv
import numpy as np
import glob, os, sys
sys.path.append('/usr/local/lib/python2.7/site-packages')
import cv2
from tqdm import tqdm
from pyquat import Quaternion
from add_landmark import add_landmark
import yaml
import matplotlib.pyplot as plt
import rosbag
from mpl_toolkits.mplot3d import Axes3D
import scipy.signal
from plot_helper import plot_3d_trajectory

def to_list(vector3):
    return [vector3.x, vector3.y, vector3.z]

def to_list4(quat):
    return [quat.w, quat.x, quat.y, quat.z]

def load_from_file(filename):
    data = np.load(filename)
    return data.item()

def save_to_file(filename, data):
    np.save(filename, data)

def make_undistort_funtion(intrinsics, resolution, distortion_coefficients):
    A = np.array([[float(intrinsics[0]), 0., float(intrinsics[2])], [0., float(intrinsics[1]), float(intrinsics[3])], [0., 0., 1.]])
    Ap, _ = cv2.getOptimalNewCameraMatrix(A, distortion_coefficients, (resolution[0], resolution[1]), 1.0)

    def undistort(image):
        return cv2.undistort(image, A, distortion_coefficients, None, Ap)

    return undistort, Ap

def calculate_velocity_from_position(t, position, orientation):
    # Calculate body-fixed velocity by differentiating position and rotating
    # into the body frame
    b, a = scipy.signal.butter(8, 0.03)  # Create a Butterworth Filter
    # differentiate Position
    delta_x = np.diff(position, axis=0)
    delta_t = np.diff(t)
    unfiltered_inertial_velocity = np.vstack((np.zeros((1, 3)), delta_x / delta_t[:, None]))
    # Filter
    v_inertial = scipy.signal.filtfilt(b, a, unfiltered_inertial_velocity, axis=0)
    # Rotate into Body Frame
    vel_data = []
    for i in range(len(t)):
        q_I_b = Quaternion(orientation[i, :, None])
        vel_data.append(q_I_b.rot(v_inertial[i, None].T).T)

    vel_data = np.array(vel_data).squeeze()
    return vel_data


def load_data(filename, start=0, end=np.inf, sim_features=False, show_image=False, plot_trajectory=True):
    print "loading rosbag", filename
    # First, load IMU data
    bag = rosbag.Bag(filename)
    imu_data = []
    truth_pose_data = []

    topic_list = ['/imu/data',
                  '/vrpn_client_node/Leo/pose',
                  '/vrpn/Leo/pose',
                  '/baro/data',
                  '/sonar/data',
                  '/is_flying',
                  '/gps/data',
                  '/mag/data']

    for topic, msg, t in tqdm(bag.read_messages(topics=topic_list), total=bag.get_message_count(topic_list) ):

        if topic == '/imu/data':
            imu_meas = [msg.header.stamp.to_sec(),
                        msg.angular_velocity.x, msg.angular_velocity.y, msg.angular_velocity.z,
                        msg.linear_acceleration.x, msg.linear_acceleration.y, msg.linear_acceleration.z]
            imu_data.append(imu_meas)

        if topic == '/vrpn_client_node/Leo/pose' or topic == '/vrpn/Leo/pose':
            truth_meas = [msg.header.stamp.to_sec(),
                          msg.pose.position.z, -msg.pose.position.x, -msg.pose.position.y,
                          -msg.pose.orientation.w, -msg.pose.orientation.z, msg.pose.orientation.x, msg.pose.orientation.y]
            truth_pose_data.append(truth_meas)


    imu_data = np.array(imu_data)
    truth_pose_data = np.array(truth_pose_data)

    # assert np.abs(truth_pose_data[0, 0] - imu_data[0, 0]) < 1e5, 'truth and imu timestamps are vastly different: {} (truth) vs. {} (imu)'.format(truth_pose_data[0, 0], imu_data[0, 0])

    # Remove Bad Truth Measurements
    good_indexes = np.hstack((True, np.diff(truth_pose_data[:,0]) > 1e-3))
    truth_pose_data = truth_pose_data[good_indexes]

    vel_data = calculate_velocity_from_position(truth_pose_data[:,0], truth_pose_data[:,1:4], truth_pose_data[:,4:8])

    ground_truth = np.hstack((truth_pose_data, vel_data))

    # Adjust timestamp
    imu_t0 = imu_data[0,0] +1
    gt_t0 = ground_truth[0,0]
    imu_data[:,0] -= imu_t0
    ground_truth[:,0] -= gt_t0

    # Chop Data to start and end
    imu_data = imu_data[(imu_data[:,0] > start) & (imu_data[:,0] < end), :]
    ground_truth = ground_truth[(ground_truth[:, 0] > start) & (ground_truth[:, 0] < end), :]

    # Simulate camera-to-body transform
    q_b_c = Quaternion.from_R(np.array([[0, 1, 0],
                                        [0, 0, 1],
                                        [1, 0, 0]]))
    p_b_c = np.array([[0.2, 0.0, 0.2]]).T

    # Simulate Landmark Measurements
    # landmarks = np.random.uniform(-25, 25, (2,3))
    landmarks = np.array([[1, 0, 1, 0, np.inf],
                          [0, 1, 1, 0, np.inf],
                          [0, 0, 1, 0, 3],
                          [1, 1, 1, 0, 3],
                          [-1, 0, 1, 10, 25],
                          [0, -1, 1, 10, 25],
                          [-1, -1, 1, 10, np.inf],
                          [1, -1, 1, 20, np.inf],
                          [-1, 1, 1, 20, np.inf]])
    feat_time, zetas, depths, ids = add_landmark(ground_truth, landmarks, p_b_c, q_b_c)

    if plot_trajectory:
        plot_3d_trajectory(ground_truth[:,1:4], ground_truth[:,4:8], qzetas=zetas, depths=depths, p_b_c=p_b_c, q_b_c=q_b_c)

    out_dict = dict()
    out_dict['imu'] = imu_data
    out_dict['truth'] = ground_truth
    out_dict['feat_time'] = feat_time
    out_dict['zetas'] = zetas
    out_dict['depths'] = depths
    out_dict['ids'] = ids
    out_dict['p_b_c'] = p_b_c
    out_dict['q_b_c'] = q_b_c

    return out_dict



if __name__ == '__main__':
    data = load_data('data/truth_imu_flight.bag')
    print "done"
