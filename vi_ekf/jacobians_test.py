from __future__ import print_function
from vi_ekf import *
import numpy as np
from tqdm import tqdm
from math_helper import q_feat_boxminus

class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

indexes = {'dxPOS': [0, 3],
           'dxVEL': [3, 6],
           'dxATT': [6, 9],
           'dxB_A': [9, 12],
           'dxB_G': [12, 15],
           'dxMU': [15, 16],
           'uA': [0, 3],
           'uG': [3, 6]}
for i in range(50):
    indexes['dxZETA_'+str(i)] = [16+3*i, 16+3*i + 2]
    indexes['dxRHO_' + str(i)] = [16+3*i+2, 16+3*i+3]

def print_error(row_idx, col_idx, analytical, fd):
    error_mat = analytical - fd
    row = indexes[row_idx]
    col = indexes[col_idx]
    if (np.abs(error_mat[row[0]:row[1], col[0]:col[1]]) > 1e-3).any():
        print (bcolors.FAIL + 'Error in Jacobian %s, %s, \nBLOCK_ERROR:\n%s\n ANALYTICAL:\n%s\n FD:\n%s\n' \
        % (row_idx, col_idx, error_mat[row[0]:row[1], col[0]:col[1]], analytical[row[0]:row[1], col[0]:col[1]], \
           fd[row[0]:row[1], col[0]:col[1]]))
        print (bcolors.ENDC)
        return 1
    else:
        return 0

def dfdx_test(x, u, ekf):
    num_errors = 0
    x0 = ekf.f(x, u)
    a_dfdx = ekf.dfdx(x, u)
    d_dfdx = np.zeros_like(a_dfdx)
    I = np.eye(d_dfdx.shape[0])
    epsilon = 1e-6

    for i in range(d_dfdx.shape[0]):
        x_prime = ekf.boxplus(x, (I[i] * epsilon)[:, None])
        d_dfdx[:, i] = ((ekf.f(x_prime, u) - x0) / epsilon)[:, 0]

    num_errors += print_error('dxPOS', 'dxVEL', a_dfdx, d_dfdx)
    num_errors += print_error('dxPOS', 'dxATT', a_dfdx, d_dfdx)
    num_errors += print_error('dxVEL', 'dxVEL', a_dfdx, d_dfdx)
    num_errors += print_error('dxVEL', 'dxATT', a_dfdx, d_dfdx)
    num_errors += print_error('dxVEL', 'dxB_A', a_dfdx, d_dfdx)
    num_errors += print_error('dxVEL', 'dxB_G', a_dfdx, d_dfdx)
    num_errors += print_error('dxVEL', 'dxMU', a_dfdx, d_dfdx)

    for i in range(ekf.len_features):
        zeta_key = 'dxZETA_' + str(i)
        rho_key = 'dxZETA_' + str(i)

        num_errors += print_error(zeta_key, 'dxVEL', a_dfdx, d_dfdx)
        num_errors += print_error(zeta_key, 'dxB_G', a_dfdx, d_dfdx)
        num_errors += print_error(zeta_key, zeta_key, a_dfdx, d_dfdx)
        num_errors += print_error(zeta_key, rho_key, a_dfdx, d_dfdx)
        num_errors += print_error(rho_key, 'dxVEL', a_dfdx, d_dfdx)
        num_errors += print_error(rho_key, 'dxB_G', a_dfdx, d_dfdx)
        num_errors += print_error(rho_key, zeta_key, a_dfdx, d_dfdx)
        num_errors += print_error(rho_key, rho_key, a_dfdx, d_dfdx)

    # This test ensures that the entire jacobian is correct, not just the blocks you test manually
    if (abs(d_dfdx - a_dfdx) > 1e-4).any():
        print (bcolors.FAIL + 'Error in Jacobian dfdx that is not caught by blocks')
        print (bcolors.FAIL + 'error: \n {} \n{}'.format(a_dfdx - d_dfdx, bcolors.ENDC))
        print (bcolors.BOLD + 'Indexes: {}'.format(np.argwhere(np.abs(a_dfdx - d_dfdx) > 1e-4)))
        num_errors += 1
        
    return num_errors

def dfdu_test(x, u, ekf):
    num_errors = 0
    a_dfdu = ekf.dfdu(x)
    d_dfdu = np.zeros_like(a_dfdu)
    I = np.eye(d_dfdu.shape[1])
    epsilon = 1e-8
    for i in range(d_dfdu.shape[1]):
        u_prime = u + (I[i] * epsilon)[:, None]
        d_dfdu[:, i] = ((ekf.f(x, u_prime) - ekf.f(x, u)) / epsilon)[:, 0]

    num_errors += print_error('dxVEL','uA', a_dfdu, d_dfdu)
    num_errors += print_error('dxVEL','uG', a_dfdu, d_dfdu)
    num_errors += print_error('dxATT', 'uG', a_dfdu, d_dfdu)

    for i in range(ekf.len_features):
        zeta_key = 'dxZETA_' + str(i)
        rho_key = 'dxZETA_' + str(i)
        num_errors += print_error(zeta_key,'uG', a_dfdu, d_dfdu)
        num_errors += print_error(rho_key,'uG', a_dfdu, d_dfdu)

    # This test ensures that the entire jacobian is correct, not just the blocks you test manually
    if np.abs(d_dfdu - a_dfdu).sum() > 1e-4:
        print(bcolors.BOLD + 'Indexes: {} {}'.format(np.argwhere(np.abs(a_dfdu - d_dfdu) > 1e-4), bcolors.ENDC))
        print (bcolors.FAIL + 'Error in overall Jacobian {} that is not caught by blocks: {}\n{}\n'.format('dfdu', bcolors.ENDC, d_dfdu - a_dfdu))
        num_errors += 1
    return num_errors

def htest(fn, **kwargs):
    num_errors = 0
    z0, analytical = fn(x, **kwargs)
    finite_difference = np.zeros_like(analytical)
    I = np.eye(finite_difference.shape[1])
    epsilon = 1e-7
    for i in range(finite_difference.shape[1]):
        x_prime = ekf.boxplus(x, (I[i] * epsilon)[:, None])
        z_prime = fn(x_prime, **kwargs)[0]
        if 'type' in kwargs.keys():
            if kwargs['type'] == 'feat':
                finite_difference[:,i] = (q_feat_boxminus(z_prime, z0) / epsilon)[:,0]
            elif kwargs['type'] == 'att':
                finite_difference[:, i] = ((Quaternion(z_prime) - Quaternion(z0)) / epsilon)[:, 0]
        else:
            finite_difference[:, i] = ((z_prime - z0) / epsilon)[:, 0]

    # The Feature Jacobian is really sensitive
    err_thresh = 5e-2 if 'type' in kwargs.keys() and kwargs['type'] == 'feat' else 1e-4

    error = analytical - finite_difference
    for key, item in indexes.iteritems():
        if item[1] > error.shape[1]:
            continue
        block_error = error[:,item[0]:item[1]]
        if (np.abs(block_error) > err_thresh).any():
            num_errors += 1
            print (bcolors.FAIL + 'Error in %s, %s, \nBLOCK_ERROR:\n%s\n ANALYTICAL:\n%s\n FD:\n%s\n'
                % (fn.__name__, key, block_error, analytical[:,item[0]:item[1]], finite_difference[:, item[0]:item[1]]))
            print (bcolors.ENDC)

    # This test ensures that the entire jacobian is correct, not just the blocks you test manually
    if (np.abs(error) > err_thresh).any():
        print(bcolors.FAIL + 'Error in overall Jacobian {} {}\n'.format(fn.__name__, bcolors.ENDC))
        print(bcolors.BOLD + 'Indexes: {} {}'.format(np.argwhere(np.abs(error) > 1e-4), bcolors.ENDC))
        print(error)
        num_errors += 1

    return num_errors

def all_h_tests(x, u, ekf):
    num_errors = 0
    num_errors += htest(ekf.h_acc)
    num_errors += htest(ekf.h_pos)
    num_errors += htest(ekf.h_vel)
    num_errors += htest(ekf.h_alt)
    num_errors += htest(ekf.h_att, type='att')
    for i in range(ekf.len_features):
        num_errors += htest(ekf.h_feat, i=i, type='feat')
        num_errors += htest(ekf.h_depth, i=i)
        num_errors += htest(ekf.h_inv_depth, i=i)
        htest(ekf.h_pixel_vel, i=i, u=u)
    return num_errors

if __name__ == '__main__':

    np.set_printoptions(suppress=True, linewidth=300, threshold=1000)

    # Set nominal inputs
    nominal_acc = np.array([[0],
                            [0],
                            [-9.80665]])
    nominal_gyro = np.array([[0.0],
                             [0.0],
                             [0.0]])
    errors = 0
    for i in range(1):
        # Set nominal Values for x0
        x0 = np.zeros((xZ, 1))
        x0[xATT] = 1
        x0[xMU] = 0.2

        # Add noise to the state
        x0[xPOS:xPOS + 3] += np.random.normal(0, 100, (3, 1))
        x0[xVEL:xVEL + 3] += np.random.normal(0, 25, (3, 1))
        x0[xB_A:xB_A + 3] += np.random.uniform(-1, 1, (3, 1))
        x0[xB_G:xB_G + 3] += np.random.uniform(-0.5, 0.5, (3, 1))
        x0[xMU,0] += np.random.uniform(-0.1, 0.1, 1)

        # Add noise to non-vector attitude states
        x0[xATT:xATT + 4] = (Quaternion(x0[xATT:xATT + 4]) + np.random.normal(0, 1, (3,1))).elements

        ekf = VI_EKF(x0)

        # Initialize Random Features
        for j in range(1):
            zeta = np.random.randn(3)[:, None]
            zeta = np.array([[0, 1.0, 1.0]]).T
            zeta /= scipy.linalg.norm(zeta)
            qzeta = Quaternion.from_two_unit_vectors(zeta, np.array([[0, 0, 1.]]).T).elements
            depth = np.abs(np.random.randn(1))[:,None]
            depth = np.ones((1,1))
            ekf.init_feature(qzeta, j, depth=depth * 10)

        # Initialize Inputs
        acc = nominal_acc + np.random.normal(0, 1, (3,1))
        gyro = nominal_gyro + np.random.normal(0, 1.0, (3, 1))

        x = ekf.x
        u = np.vstack([acc, gyro])

        # Print Test Configuration
        # print('x = %s' % x.T)
        # print('u = %s' % u.T)

        # Check Jacobians
        errors += dfdx_test(x, u, ekf)
        errors += dfdu_test(x, u, ekf)
        errors += all_h_tests(x, u, ekf)

    if errors == 0:
        print(bcolors.OKGREEN + "[PASSED]" + bcolors.ENDC)
    else:
        print(bcolors.FAIL + "[FAILED] %d tests" % errors)