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

#include "Track.h"

using namespace MPF::COMPONENT;

string format(cv::Mat m);

/** ***************************************************************************
*  Constructor
*
* \param detPtr a 1st detection to add to the new track
* \param cfg    job struct to initialize kalman filter params from
*
**************************************************************************** */
Track::Track(const ConfigPtr cfgPtr, unique_ptr<DetectionLocation> detPtr):
  _cfgPtr(cfgPtr){
  if(! cfgPtr->kfDisabled){
    _kfPtr = unique_ptr<KFTracker>(
      new KFTracker(detPtr->framePtr->time,
                    detPtr->framePtr->timeStep,
                    detPtr->getRect(),
                    cv::Rect2i(0,0,detPtr->framePtr->bgr.cols-1,
                                   detPtr->framePtr->bgr.rows-1),
                    cfgPtr->RN,
                    cfgPtr->QN));
  }
  _locationPtrs.push_back(move(detPtr));
}

/** **************************************************************************
*  Get a new DetectionLocation from an existing one based on a frame
*
* \param framePtr pointer to the frame with new tracking info
*
* \returns ptr to new location based on tracker's estimation
*
* \note    tracker is passed on to the new location on success
*
**************************************************************************** */
DetectionLocationPtr Track::ocvTrackerPredict(const FramePtr &framePtr){

  if(_trackerPtr.empty()){   // initialize a new tracker if we don't have one already
    cv::Rect2i bbox    = _locationPtrs.back()->getRect();
    cv::Rect2i overlap =  bbox & cv::Rect2i(0,0,_locationPtrs.back()->framePtr->bgr.cols -1,
                                                _locationPtrs.back()->framePtr->bgr.rows -1);
     if(overlap.width > 1 && overlap.height > 1){
       _trackerPtr = cv::TrackerMOSSE::create();                               // could try different trackers here. e.g. cv::TrackerKCF::create();
       _trackerPtr->init(_locationPtrs.back()->framePtr->bgr, bbox);           LOG_TRACE("tracker created for " << (MPFImageLocation)*_locationPtrs.back());
       _trackerStartFrameIdx = framePtr->idx;
    }else{                                                                     LOG_TRACE("can't create tracker created for " << (MPFImageLocation)*_locationPtrs.back());
      return nullptr;
    }
  }

  cv::Rect2d p;
  DetectionLocationPtr detPtr;
  if(framePtr->idx -_trackerStartFrameIdx <= _cfgPtr->maxFrameGap){
    if(_trackerPtr->update(framePtr->bgr, p)){
      cv::Point2f center(p.tl() + cv::Point2d(p.size()) / 2.0);
      center.x /= static_cast<float>(framePtr->bgr.cols);
      center.y /= static_cast<float>(framePtr->bgr.rows);
      detPtr = DetectionLocationPtr(
                 new DetectionLocation(_cfgPtr, framePtr, p, 0.0, center));    LOG_TRACE("tracking " << (MPFImageLocation)*_locationPtrs.back() << " to " << (MPFImageLocation)*detPtr);
      // clone feature of prior detection
      detPtr->copyFeature(*(_locationPtrs.back()));
    }else{
                                                                               LOG_TRACE("could not track " << (MPFImageLocation)*_locationPtrs.back() << " to new location");
    }
  }else{
                                                                               LOG_TRACE("extrapolation tracking stopped" << (MPFImageLocation)*_locationPtrs.back() << " frame gap = " << framePtr->idx - _trackerStartFrameIdx << " > " <<  _cfgPtr->maxFrameGap);
  }
  return detPtr;
}

/** **************************************************************************
* Add detection pointer to the end of the track
*
* \param d pointer to the detection to append to the track
*
* \note the previous track tail will have its image frame released
*************************************************************************** */
void Track::push_back(DetectionLocationPtr d){

  assert(_locationPtrs.size() > 0);
  //const_cast<cv::Mat&>(_locationPtrs.back()->framePtr->bgr).release();          // don't access pixels after this...
  _locationPtrs.push_back(move(d));
}

/** **************************************************************************
 * Advance Kalman filter state to predict next bbox at time t
 *
 * \param t time in sec to which kalamn filter state is advanced to
 *
*************************************************************************** */
void Track::kalmanPredict(float t){
  _kfPtr->predict(t);                                                          LOG_TRACE("kf pred:" << _locationPtrs.back()->getRect() << " => " << _kfPtr->predictedBBox());
}

/** **************************************************************************
 * apply kalman correction to tail detection using tail's measurement
*************************************************************************** */
void Track::kalmanCorrect(){
  if(_kfPtr){                                                                  LOG_TRACE("kf meas:" << _locationPtrs.back()->getRect());
    _kfPtr->correct(_locationPtrs.back()->getRect());                          LOG_TRACE("kf corr:" << _locationPtrs.back()->getRect());
    back()->setRect(_kfPtr->correctedBBox());
  }
}
