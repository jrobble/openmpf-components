/******************************************************************************
 * NOTICE                                                                     *
 *                                                                            *
 * This software (or technical data) was produced for the U.S. Government     *
 * under contract, and is subject to the Rights in Data-General Clause        *
 * 52.227-14, Alt. IV (DEC 2007).                                             *
 *                                                                            *
 * Copyright 2018 The MITRE Corporation. All Rights Reserved.                 *
 ******************************************************************************/

/******************************************************************************
 * Copyright 2018 The MITRE Corporation                                       *
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

#include <iostream>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <dlfcn.h>
#include <string>
#include <vector>
#include "SceneChangeDetection.h"
#include <QDir>


using namespace MPF::COMPONENT;
using namespace std;

template<typename T>
vector<T> 
split(const T & str, const T & delimiters) {
    vector<T> v;
    typename T::size_type start = 0;
    auto pos = str.find_first_of(delimiters, start);
    while(pos != T::npos) {
        if(pos != start) // ignore empty tokens
            v.emplace_back(str, start, pos - start);
        start = pos + 1;
        pos = str.find_first_of(delimiters, start);
    }
    if(start < str.length()) // ignore trailing delimiter
        v.emplace_back(str, start, str.length() - start); // add what's left of the string
    return v;
};

int main(int argc, char* argv[]) {

    if ((argc < 2) || (argc > 5)) {
        std::cout << "argc = " << argc << std::endl;
        std::cout << "Usage: " << argv[0] << " <image URI> <num classifications> <confidence threshold> <ROTATE | CROP | FLIP>" << std::endl;
        return EXIT_SUCCESS;
    }

    SceneChangeDetection scene_change_component;



    QHash<QString, QString> parameters;
    QString current_path = QDir::currentPath();
    std::string config_path(current_path.toStdString() + "/config/mog_config.ini");

    scene_change_component.SetRunDirectory("plugin");

    if (!scene_change_component.Init()) {
        std::cout << "Component initialization failed, exiting." << std::endl;
        return EXIT_FAILURE;
    }

    Properties algorithm_properties;
    
    std::string uri(argv[1]);
    std::cout << "uri is " << uri << std::endl;

    vector<string> v = split<string>(uri,".");
    auto lastel = v.back();
    MPFDetectionDataType media_type;
    
  
    {
        std::vector<MPFVideoTrack> detections;
        media_type = VIDEO;
        Properties media_properties;
        std::string job_name("Testing Scene Change");
        std::cout<<"testing scene change"<<std::endl;
        MPFVideoJob job(job_name, uri, 0,251,algorithm_properties, media_properties);
        scene_change_component.GetDetections(job, detections);
        std::cout<<"number of final scenes: "<<detections.size()<<std::endl;
        for (int i = 0; i < detections.size(); i++) {
            std::cout << "scene number "
                      << i
                      << ": start frame is "
                      << detections[i].start_frame
                        << "; stop frame is "
                      << detections[i].stop_frame
                      << std::endl;
        }

    }
   

    
    scene_change_component.Close();
    return EXIT_SUCCESS;
}

