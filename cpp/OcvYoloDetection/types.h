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

#ifndef OCVYOLODETECTION_TYPES_H
#define OCVYOLODETECTION_TYPES_H

#include <type_traits>
#include <memory>
#include <list>
#include <iomanip>
#include <adapters/MPFImageAndVideoDetectionComponentAdapter.h>

namespace MPF{
 namespace COMPONENT{

  using namespace std;

  class Frame;
  class Config;
  class DetectionLocation;
  class Track;

  typedef vector<string>                  stringVec;                   ///< vector of strings
  typedef vector<float>                   floatVec;                    ///< vector of floats
  typedef vector<int>                     intVec;                      ///< vector of integers
  typedef vector<MPFVideoTrack>           MPFVideoTrackVec;            ///< vector of MPFVideoTracks
  typedef vector<MPFImageLocation>        MPFImageLocationVec;         ///< vector of MPFImageLocations
  typedef vector<MPFImageLocationVec>     MPFImageLocationVecVec;      ///< vector of MPFImageLocations vectors
  typedef vector<cv::Mat>                 cvMatVec;                    ///< vector of OpenCV matrices/images
  typedef vector<cvMatVec>                cvMatVecVec;                 ///< vector of OpenCV matrices vectors
  typedef vector<cv::Rect2d>              cvRect2dVec;                 ///< vector of OpenCV rectangles
  typedef vector<cv::Point2i>             cvPoint2iVec;                ///< vector of OpenCV points
  typedef vector<cv::Point2f>             cvPoint2fVec;                ///< vector of OpenCV 2D float points
  typedef vector<cvPoint2fVec>            cvPoint2fVecVec;             ///< vector of vectors of OpenCV 2D float points
  typedef cv::Point3_<uint8_t>            cvPixel;                     ///< image pixel type used by images
  typedef unique_ptr<DetectionLocation>   DetectionLocationPtr;        ///< DetectionLocation pointers
  typedef vector<DetectionLocationPtr>    DetectionLocationPtrVec;     ///< vector of DetectionLocation pointers
  typedef vector<DetectionLocationPtrVec> DetectionLocationPtrVecVec;  ///< vector of DetectionLocation pointers vectors
  typedef list<unique_ptr<Track>>         TrackPtrList;                ///< list of track pointers
  typedef shared_ptr<const Config>        ConfigPtr;                   ///< Config pointer (state)
  typedef shared_ptr<const Frame>         FramePtr;                    ///< frame pointer
  typedef vector<FramePtr>                FramePtrVec;                 ///< vector for Frame pointers

  /** ****************************************************************************
   *  print out opencv matrix on a single line
   *
   * \param   m matrix to serialize to single line string
   * \returns single line string representation of matrix
   *
  ***************************************************************************** */
  inline
  string format(cv::Mat1f m){
    stringstream ss;

    ss << "[";
    for(int r=0; r<m.rows; r++){
      for(int c=0; c<m.cols; c++){
        ss << setfill('0') << setw(6) << fixed << setprecision(3) << m.at<float>(r,c);
        if(c!=m.cols-1){
          ss << ", ";
        }else if(r!=m.rows-1){
          ss << "; ";
        }
      }
    }
    ss << "]";
    //   << m;
    string str = ss.str();
    //str.erase(remove(str.begin(),str.end(),'\n'),str.end());

    return str;
  }

  /** **************************************************************************
  *   Dump MPFLocation to a stream
  *************************************************************************** */
  inline
  ostream& operator<< (ostream& os, const MPFImageLocation& l) {
    os << "[" << l.x_left_upper << "," << l.y_left_upper << "]-("
               << l.width        << "," << l.height       << "):"
               << l.confidence;
    if(l.detection_properties.find("CLASSIFICATION") != l.detection_properties.end()){
      os << "|" << l.detection_properties.at("CLASSIFICATION");
    }

    return os;
  }

  /** **************************************************************************
  *   Dump MPFTrack to a stream
  *************************************************************************** */
  inline
  ostream& operator<< (ostream& os, const MPFVideoTrack& t) {
    os << t.start_frame << endl;
    os << t.stop_frame  << endl;
    for(auto& p:t.frame_locations){
      os << p.second.x_left_upper << "," << p.second.y_left_upper << ","
          << p.second.width << "," << p.second.height << endl;
    }
    return os;
  }

  /** ****************************************************************************
  *   Dump vectors to a stream
  ***************************************************************************** */
  template<typename T> inline
  ostream& operator<< (ostream& os, const vector<T>& v) {
    os << "{";
    size_t last = v.size() - 1;
    for(size_t i = 0; i < v.size(); ++i){
      os << v[i];
      if(i != last) os << ", ";
    }
    os << "}";
    return os;
  }
 }
}
#endif