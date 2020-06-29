/******************************************************************************
 * NOTICE                                                                     *
 *                                                                            *
 * This software (or technical data) was produced for the U.S. Government     *
 * under contract, and is subject to the Rights in Data-General Clause        *
 * 52.227-14, Alt. IV (DEC 2007).                                             *
 *                                                                            *
 * Copyright 2020 The MITRE Corporation. All Rights Reserved.                 *
 ******************************************************************************/

/******************************************************************************
 * Copyright 2020 The MITRE Corporation                                       *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License");            *
 * you may not use this file except in compliance with the License.           *
 * You may obtain a copy of the License at                                    *
 *                                                                            *
 *    http://www.apache.org/licenses/LICENSE-2.0                              *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 ******************************************************************************/

#include "KFTracker.h"

#define PROCESS_NOISE CONTINUOUS_WHITE
//#define PROCESS_NOISE PIECEWISE_WHITE

using namespace MPF::COMPONENT;

log4cxx::LoggerPtr KFTracker::_log;

// Kalman Filter Dimensions (4x constant acceleration model)
const int KF_STATE_DIM = 12; ///< dimensionality of kalman state vector       [x, v_x, ,a_x, y, v_y, a_y, w, v_w, a_w, h, v_h, ah]
const int KF_MEAS_DIM  = 4;  ///< dimensionality of kalman measurement vector [x, y, w, h]
const int KF_CTRL_DIM  = 0;  ///< dimensionality of kalman control input      []

/** **************************************************************************
 * Update the model state transision matrix F and the model noise covariance
 * matrix Q to be suitable of a timestep of dt
 *
 * \param dt new time step in sec
 *
 * \note F and Q are block diagonal with 4 blocks, one for each x, y, w, h
 *
*************************************************************************** */
void KFTracker::_setTimeStep(float dt){

  if(fabs(_dt - dt) > 2 * numeric_limits<float>::epsilon()){
    _dt = dt;                                                                  LOG4CXX_TRACE(_log,"kd dt:" << _dt);

    float dt2 = dt  * dt;
    float dt3 = dt2 * dt;
    float dt4 = dt2 * dt2;

    float half_dt2 = 0.5*dt2;

    #if PROCESS_NOISE == CONTINUOUS_WHITE
      float  third_dt3 = dt3 / 3.0;
      float  sixth_dt3 = dt3 / 6.0;
      float eighth_dt4 = dt4 / 8.0;
      float twentieth_dt5 = dt2 * dt3 / 20.0;
    #elif PROCESS_NOISE == PIECEWISE_WHITE
      float    half_dt3 = dt3 / 2.0;
      float quarter_dt4 = dt4 / 4.0;
    #endif

    for(int b=0; b < 4; b++){
      int i = 3*b;
      // update state transition matrix F
      _kf.transitionMatrix.at<float>(   i, 1+i) =
      _kf.transitionMatrix.at<float>( 1+i, 2+i) = dt;
      _kf.transitionMatrix.at<float>(   i, 2+i) = half_dt2;

      // update process noise matrix Q
      #if PROCESS_NOISE == CONTINUOUS_WHITE
        _kf.processNoiseCov.at<float>(  i,  i) = _qn.at<float>(b,0) * twentieth_dt5;
        _kf.processNoiseCov.at<float>(1+i,  i) =
        _kf.processNoiseCov.at<float>(  i,1+i) = _qn.at<float>(b,0) * eighth_dt4;
        _kf.processNoiseCov.at<float>(2+i,  i) =
        _kf.processNoiseCov.at<float>(  i,2+i) = _qn.at<float>(b,0) * sixth_dt3;
        _kf.processNoiseCov.at<float>(1+i,1+i) = _qn.at<float>(b,0) * third_dt3;
        _kf.processNoiseCov.at<float>(1+i,2+i) =
        _kf.processNoiseCov.at<float>(2+i,1+i) = _qn.at<float>(b,0) * half_dt2;
        _kf.processNoiseCov.at<float>(2+i,2+i) = _qn.at<float>(b,0) * dt;
      #elif PROCESS_NOISE == PIECEWISE_WHITE
        _kf.processNoiseCov.at<float>(  i,  i) = _qn.at<float>(b,0) * quarter_dt4;
        _kf.processNoiseCov.at<float>(1+i,  i) =
        _kf.processNoiseCov.at<float>(  i,1+i) = _qn.at<float>(b,0) * half_dt3;
        _kf.processNoiseCov.at<float>(2+i,  i) =
        _kf.processNoiseCov.at<float>(  i,2+i) = _qn.at<float>(b,0) * half_dt2;
        _kf.processNoiseCov.at<float>(1+i,1+i) = _qn.at<float>(b,0) * dt2;
        _kf.processNoiseCov.at<float>(1+i,2+i) =
        _kf.processNoiseCov.at<float>(2+i,1+i) = _qn.at<float>(b,0) * dt;
        _kf.processNoiseCov.at<float>(2+i,2+i) = _qn.at<float>(b,0);
      #endif
    }

  }
}

/** **************************************************************************
 * Convert bbox to a measurement vector consisting of center coordinate and
 * width and height dimensions
 *
 * \param r bounding box to convert
 *
 * \returns measurement vector
 *
*************************************************************************** */
cv::Mat_<float> KFTracker::measurementFromBBox(const cv::Rect2i& r){
  cv::Mat_<float> z(KF_MEAS_DIM,1);
  z.at<float>(0) = r.x + r.width  / 2.0f;
  z.at<float>(1) = r.y + r.height / 2.0f;
  z.at<float>(2) = r.width;
  z.at<float>(3) = r.height;
  return z;
}

/** **************************************************************************
 * Convert a filter state [ x,vx,ax, y,vy,ay, w,wv,aw, h,vh,ah] to roi(x,y,w,h)
 *                          0  1  2  3  4  5  6  7  8  9 10 11
 *
 * \param state filter state vector to convert
 *
 * \returns bounding box corresponding to state
 *
*************************************************************************** */
cv::Rect2i KFTracker::bboxFromState(const cv::Mat_<float> state){
  return cv::Rect2i(static_cast<int>(state.at<float>(0) - state.at<float>(6)/2.0f + 0.5f),
                    static_cast<int>(state.at<float>(3) - state.at<float>(9)/2.0f + 0.5f),
                    static_cast<int>(state.at<float>(6)                 + 0.5f),
                    static_cast<int>(state.at<float>(9)                 + 0.5f));
}

/** **************************************************************************
 * Get bounding box prediction by advancing filter state to time t
 * and update filter time to t
 *
 * \param t time in sec to advance filter to with prediction
 *
*************************************************************************** */
void KFTracker::predict(float t){
  _setTimeStep(t - _t);
  _t = t;
  _kf.predict();
}

/** **************************************************************************
 * Get bounding box from state after corrected by a measurement at filter time t
 *
 * \param rec measurement to use for correcting filter state at current filter
 *            time
 *
 * \note filter will record and dump error statistics in _state_trace if
 *       compiled for debug
 *
*************************************************************************** */
void KFTracker::correct(const cv::Rect2i &rec){
  _kf.correct(measurementFromBBox(rec));
  #ifndef NDEBUG
    _state_trace << (*this) << endl;
  #endif
}

/** **************************************************************************
 * Construct and initialize kalman filter motion tracker
 *
 * \param t    time corresponding to initial bounding box rec0
 * \param dt   timestep to use for the filter (sec)
 * \param rec0 inital bounding box measurement
 * \param roi  clipping constraints so BBoxes don't wonder off frame
 * \param rn   4x1 column vector of variances for measurement noise, var([x,y,w,h])
 * \param qn   4x1 column vector of variances for model noise, var([ax,ay,aw,ah])
 *
*************************************************************************** */
KFTracker::KFTracker(const float t,
                     const float dt,
                     const cv::Rect2i &rec0,
                     const cv::Rect2i &roi,
                     const cv::Mat_<float> &rn,
                     const cv::Mat_<float> &qn):
  _t(t),
  _dt(-1.0f),
  _roi(roi),
  _qn(qn),
  _kf(KF_STATE_DIM,KF_MEAS_DIM,KF_CTRL_DIM,CV_32F)       // kalman(state_dim,meas_dim,contr_dim)
{
    assert(_log);
    assert(  rn.rows == KF_MEAS_DIM &&   rn.cols == 1);
    assert(  qn.rows == KF_MEAS_DIM &&   qn.cols == 1);  // only accelerations model noise should be specified

    assert(_roi.x == 0);
    assert(_roi.y == 0);
    assert(_roi.width  > 0);
    assert(_roi.height > 0);

    // state transition matrix F
    //    | 0  1    2   3  4    5    6  7    8    9 10   11
    //--------------------------------------------------------
    //  0 | 1 dt .5dt^2 0  0    0    0  0    0    0  0    0   |   | x|
    //  1 | 0  1   dt   0  0    0    0  0    0    0  0    0   |   |vx|
    //  2 | 0  0    1   0  0    0    0  0    0    0  0    0   |   |ax|
    //  3 | 0  0    0   1 dt .5dt^2  0  0    0    0  0    0   |   | y|
    //  4 | 0  0    0   0  1   dt    0  0    0    0  0    0   |   |vy|
    //  5 | 0  0    0   0  0    1    0  0    0    0  0    0   |   |vy|
    //  6 | 0  0    0   0  0    0    1 dt .5dt^2  0  0    0   |   | w|
    //  7 | 0  0    0   0  0    0    0  1   dt    0  0    0   |   |vw|
    //  8 | 0  0    0   0  0    0    0  0    1    0  0    0   |   |aw|
    //  9 | 0  0    0   0  0    0    0  0    0    1 dt .5dt^2 |   | h|
    // 10 | 0  0    0   0  0    0    0  0    0    0  1   dt   |   |vh|
    // 11 | 0  0    0   0  0    0    0  0    0    0  0    1   |   |ah|

    // measurement matrix H
    //      0  1  2  3  4  5  6  7  8  9 10 11
    //-----------------------------------------
    //  0 | 1  0  0  0  0  0  0  0  0  0  0  0 |       | x |
    //  1 | 0  0  0  1  0  0  0  0  0  0  0  0 |       | y |
    //  2 | 0  0  0  0  0  0  1  0  0  0  0  0 | * A = | w |
    //  3 | 0  0  0  0  0  0  0  0  0  1  0  0 |       | h |
    _kf.measurementMatrix.at<float>(0,0) =
    _kf.measurementMatrix.at<float>(1,3) =
    _kf.measurementMatrix.at<float>(2,6) =
    _kf.measurementMatrix.at<float>(3,9) = 1.0f;

    // measurement noise covariance matrix R
    //    | 0   1  2  3
    //------------------
    //  0 | xx  0  0  0
    //  1 |  0 yy  0  0
    //  2 |  0  0 ww  0
    //  3 |  0  0  0  hh
    _kf.measurementNoiseCov.at<float>(0,0) = rn.at<float>(0);
    _kf.measurementNoiseCov.at<float>(1,1) = rn.at<float>(1);
    _kf.measurementNoiseCov.at<float>(2,2) = rn.at<float>(2);
    _kf.measurementNoiseCov.at<float>(3,3) = rn.at<float>(3);

    //adjust F and setup process noise covariance matrix Q
    _setTimeStep(dt);

    //initialize filter state
    cv::Mat_<float> z0 = measurementFromBBox(rec0);
    _kf.statePost.at<float>(0) = z0.at<float>(0);
    _kf.statePost.at<float>(3) = z0.at<float>(1);
    _kf.statePost.at<float>(6) = z0.at<float>(2);
    _kf.statePost.at<float>(9) = z0.at<float>(3);

    //initialize error covariance matrix P
    // z: [ x, y, w, h]
    // x: [ x,vx,ax, y,vy,ay, w,wv,aw, h,vh,ah]
    //      0  1  2  3  4  5  6  7  8  9 10 11
    _kf.errorCovPost.at<float>(0,0) = rn.at<float>(0);
    _kf.errorCovPost.at<float>(1,1) = (z0.at<float>(2)/dt) * (z0.at<float>(2)/dt);    // guess max vx as one width/dt
    _kf.errorCovPost.at<float>(2,2) = 10.0 * _kf.processNoiseCov.at<float>(2,2);      // guess ~3 sigma ?!

    _kf.errorCovPost.at<float>(3,3) = rn.at<float>(1);
    _kf.errorCovPost.at<float>(4,4) = (z0.at<float>(3)/dt) * (z0.at<float>(3)/dt);   // guess max vy as one height/dt
    _kf.errorCovPost.at<float>(5,5) = 10.0 * _kf.processNoiseCov.at<float>(5,5);     // guess ~3 sigma ?!

    _kf.errorCovPost.at<float>(6,6) = rn.at<float>(2);
    _kf.errorCovPost.at<float>(7,7) = 10.0 * _kf.processNoiseCov.at<float>(7,7);     // guess ~3 sigma
    _kf.errorCovPost.at<float>(8,8) = 10.0 * _kf.processNoiseCov.at<float>(8,8);     // guess ~3 sigma

    _kf.errorCovPost.at<float>(9,9)   = rn.at<float>(3);
    _kf.errorCovPost.at<float>(10,10) = 10.0 * _kf.processNoiseCov.at<float>(10,10); // guess ~3 sigma
    _kf.errorCovPost.at<float>(11,11) = 10.0 * _kf.processNoiseCov.at<float>(11,11); // guess ~3 sigma

    #ifndef NDEBUG
      _state_trace << (*this) << endl;                    // trace filter initial error stats
      _myId = _objId;                                     // used for output filename
      _objId++;
    #endif
}

/** **************************************************************************
* Setup class shared static configurations and initialize
*
* \param log         logger object for logging
* \param plugin_path plugin directory
*
* \return true if everything was properly initialized, false otherwise
*************************************************************************** */
bool KFTracker::Init(log4cxx::LoggerPtr log, string plugin_path=""){

  _log = log;

  return true;
}

#ifndef NDEBUG

#include <fstream>
size_t KFTracker::_objId = 0;  // class static sequence variable

/** **************************************************************************
*  Write out error statisics over time to csv file for filter tuning
*************************************************************************** */
void KFTracker::dump(){
  stringstream filename;
  filename << "kf_" << setfill('0') << setw(4) << _myId;
  filename << ".csv";
  ofstream dump;
  dump.open(filename.str(),ofstream::out | ofstream::trunc);
  dump << "t,";
  dump << "px,pvx,pax, py,pvy,pay, pw,pvw,paw, ph,pvh,pah, ";
  dump << "cx,cvx,cax, cy,cvy,cay, cw,cvw,caw, ch,cvh,cah, ";
  dump << "err_x, err_y, err_w, err_h,";
  for(int r=0;r<12;r++){
      dump << "P" << setfill('0') << setw(2) << r << "_" << r ;
      dump << ",";
  }
  dump << endl;
  dump << _state_trace.rdbuf();
  dump.close();
}
/** **************************************************************************
*   Dump Diagnostics for MPF::COMPONENT::KFTracker to a stream
*************************************************************************** */
ostream& MPF::COMPONENT::operator<< (ostream& out, const KFTracker& kft) {
  out << kft._t << ",";

  for(int i=0; i < kft._kf.statePre.rows; i++){
    out << kft._kf.statePre.at<float>(i) <<",";
  }
  out << " ";

  for(int i=0; i < kft._kf.statePost.rows; i++){
    out << kft._kf.statePost.at<float>(i) <<",";
  }
  out << " ";

  for(int i=0; i < kft._kf.temp5.rows; i++){
    out << kft._kf.temp5.at<float>(i) << ",";
  }
  out << " ";

  for(int r=0; r<kft._kf.errorCovPost.rows; r++){
    //for(int c=0; c<kft._kf.errorCovPost.cols; c++){
      out << sqrt(kft._kf.errorCovPost.at<float>(r,r)) << ",";
    //}
  }
  return out;
}
#endif